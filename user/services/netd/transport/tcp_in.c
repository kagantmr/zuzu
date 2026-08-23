#include "../common/netrand.h"
#include "../net/ip.h"
#include "port.h"
#include "tcp.h"
#include "tcp_out.h"
#include "tcp_opts.h"
#include "tcp_pcb.h"
#include <convert.h>
#include <string.h>
#include <zuzu/log.h>

/**
 * OOO helpers
 */

/* delete ranges[i], slide the tail down */
static void RangesDelete(TcpPcb *pcb, size_t i)
{
    memmove(&pcb->ranges[i], &pcb->ranges[i + 1], (pcb->nranges - i - 1) * sizeof(pcb->ranges[0]));
    pcb->nranges--;
}

static void FwdMerge(TcpPcb *pcb)
{
    while (pcb->nranges > 0 && seq_leq(pcb->ranges[0].start, pcb->rcv_nxt)) {
        pcb->rcv_nxt = seq_max(pcb->rcv_nxt, pcb->ranges[0].end);
        RangesDelete(pcb, 0);
    }
}

static void StoreOoo(TcpPcb *pcb, const TcpSegment *s)
{
    uint32_t seg_start = s->seq;
    uint32_t seg_end = s->seq + s->payload_len;

    /* left-truncate the SEGMENT against rcv_nxt */
    if (seq_leq(seg_end, pcb->rcv_nxt))
        return;                          // wholly old, drop
    if (seq_lt(seg_start, pcb->rcv_nxt)) // partially old
        seg_start = pcb->rcv_nxt;        // clip left edge forward

    /* case 2: future data (hole before it)  */
    uint32_t reach = seg_end - pcb->rcv_rsq;
    if (reach > TCP_RCV_BUF) {
        LOG_INFO(LOG_TAG, "OOO seg beyond window, dropping: reach=%u", reach);
        return; /* peer will retransmit */
    }

    if (pcb->nranges >= TCP_OOO_MAX)
        return; /* full, drop for backpressure */

    uint16_t clip = seg_start - s->seq;       // bytes trimmed off the front (0 if no truncation)
    uint16_t seg_len = s->payload_len - clip; // bytes we actually store
    const uint8_t *p = s->payload + clip;     // source, shifted past the trimmed part

    size_t off = seg_start & (TCP_RCV_BUF - 1);
    size_t first = MIN(seg_len, TCP_RCV_BUF - off);
    memcpy(pcb->rcv_buf + off, p, first);
    if (first < seg_len)
        memcpy(pcb->rcv_buf, p + first, seg_len - first);

    size_t i = 0;
    while (i < pcb->nranges && seq_lt(pcb->ranges[i].start, seg_start))
        i++;

    memmove(&pcb->ranges[i + 1], &pcb->ranges[i], (pcb->nranges - i) * sizeof(pcb->ranges[0]));
    pcb->ranges[i].start = seg_start;
    pcb->ranges[i].end = seg_end;
    pcb->nranges++;

    /* phase 2: fuse touching neighbors in one pass */
    for (size_t k = 0; k + 1 < pcb->nranges;) {
        if (seq_leq(pcb->ranges[k + 1].start, pcb->ranges[k].end)) {
            pcb->ranges[k].end = seq_max(pcb->ranges[k].end, pcb->ranges[k + 1].end);
            RangesDelete(pcb, k + 1); /* don't advance k, re-check new neighbor */
        } else {
            k++;
        }
    }
}

/* A received segment, parsed once and passed to the per-state handlers. */
/* ------------------------------------------------------------------ */
/* helpers                                                            */
/* ------------------------------------------------------------------ */

static void time_wait_cb(void *arg)
{
    TcpPcb *pcb = (TcpPcb *)arg;
    rto_stop(pcb);
    port_release(pcb->local_port);
    tcp_pcb_free(tcp_pcb_index(pcb));
    LOG_INFO(LOG_TAG, "TIME_WAIT expired, connection freed");
}

/* Copy in-order payload into the receive ring and advance rcv_nxt. */
static void deliver_data(TcpPcb *pcb, const uint8_t *payload, uint16_t payload_len)
{
    size_t used = pcb->rcv_nxt - pcb->rcv_rsq;
    size_t free = TCP_RCV_BUF - used;
    size_t n = MIN(payload_len, free);
    size_t off = pcb->rcv_nxt & (TCP_RCV_BUF - 1);
    size_t first = MIN(n, TCP_RCV_BUF - off);
    memcpy(pcb->rcv_buf + off, payload, first);
    if (first < n)
        memcpy(pcb->rcv_buf, payload + first, n - first);
    pcb->rcv_nxt += n;
}

static void consume_fin(int slot, TcpPcb *pcb)
{
    if (!pcb->fin_seen)
        return;
    if (pcb->rcv_nxt != pcb->fin_seq)
        return;
    pcb->rcv_nxt += 1;
    tcp_output(pcb, TCP_ACK, NULL, 0);

    if (pcb->state == TCP_ESTABLISHED) {
        pcb->state = TCP_CLOSE_WAIT;
        tcp_close(slot);
    } else if (pcb->state == TCP_FIN_WAIT_1) {
        pcb->state = TCP_TIME_WAIT;
        timer_arm(net_now_ms() + TCP_TIME_WAIT_MS, time_wait_cb, pcb);
    }
}

/* ------------------------------------------------------------------ */
/* per-state handlers                                                 */
/* ------------------------------------------------------------------ */

static void on_syn_sent(TcpPcb *pcb, const TcpSegment *s)
{
    if (((s->flags & TCP_SYN) && (s->flags & TCP_ACK)) && s->ack == pcb->snd_nxt) {
        pcb->rcv_nxt = s->seq + 1;
        pcb->rcv_rsq = pcb->rcv_nxt;
        pcb->snd_una = s->ack;
        pcb->state = TCP_ESTABLISHED;
        tcp_output(pcb, TCP_ACK, NULL, 0);
    }
}

static void TcpRttUpdate(TcpPcb *pcb, uint32_t R)
{
    if (!pcb->rtt_valid) {
        /* first sample ever: seed directly */
        pcb->srtt = R;
        pcb->rttvar = R / 2;
        pcb->rtt_valid = true;
    } else {
        /* |srtt - R| without signed trouble */
        uint32_t diff = (pcb->srtt > R) ? (pcb->srtt - R) : (R - pcb->srtt);
        pcb->rttvar = (3 * pcb->rttvar + diff) / 4; /* 3/4 old + 1/4 new */
        pcb->srtt = (7 * pcb->srtt + R) / 8;        /* 7/8 old + 1/8 new */
    }

    /* RTO = srtt + 4*rttvar, clamped to a sane floor and your existing cap */
    Duration rto = pcb->srtt + 4 * pcb->rttvar;
    if (rto < 1000)
        rto = 1000; /* granularity floor */
    if (rto > TCP_RTO_MAX)
        rto = TCP_RTO_MAX;
    pcb->rto_ms = rto;

    LOG_INFO(LOG_TAG, "RTT sample R=%u srtt=%u rttvar=%u rto=%u", R, pcb->srtt, pcb->rttvar,
             pcb->rto_ms);
}

static void on_established(int slot, TcpPcb *pcb, const TcpSegment *s)
{
    if (seq_lt(pcb->snd_una, s->ack) && seq_leq(s->ack, pcb->snd_nxt)) {
        size_t delta = s->ack - pcb->snd_una; // how many bytes got confirmed

        if (pcb->rtt_timing && seq_leq(pcb->rtt_seq, s->ack)) {
            uint32_t R = net_now_ms() - pcb->rtt_start;
            TcpRttUpdate(pcb, R);
            pcb->rtt_timing = false;
        }

        pcb->snd_una = s->ack;
        pcb->buffered_bytes -= MIN(delta, pcb->buffered_bytes);
        if (pcb->snd_nxt == pcb->snd_una) {
            rto_stop(pcb);
            LOG_INFO(LOG_TAG, "data acked, RTO cancelled");
        } else {
            rto_stop(pcb);
            rto_start(pcb);
            LOG_INFO(LOG_TAG, "partially acked, RTO restarted");
        }
    }

    if (seq_lt(pcb->snd_nxt, s->ack)) {
        tcp_output(pcb, TCP_ACK, NULL, 0);
        return;
    }

    if (s->payload_len) {
        if (s->seq == pcb->rcv_nxt) {
            deliver_data(pcb, s->payload, s->payload_len);
            FwdMerge(pcb);
            tcp_output(pcb, TCP_ACK, NULL, 0); /* ack what we got */

            /* TODO: application logic wired directly into the transport.
       * This canned HTTP reply should move behind an app-layer callback. */
            if (pcb->on_data)
                pcb->on_data(slot);

            consume_fin(slot, pcb);
        } else if (seq_lt(pcb->rcv_nxt, s->seq)) {
            StoreOoo(pcb, s);
            LOG_INFO(LOG_TAG, "OOO seg: seq=%u rcv_nxt=%u len=%u", s->seq, pcb->rcv_nxt,
                     s->payload_len);
            tcp_output(pcb, TCP_ACK, NULL, 0); /* dup-ACK: still want rcv_nxt */
        } else {
            /* case 3: old duplicate */
            LOG_INFO(LOG_TAG, "dup seg: seq=%u rcv_nxt=%u len=%u", s->seq, pcb->rcv_nxt,
                     s->payload_len);
            tcp_output(pcb, TCP_ACK, NULL, 0); /* re-ACK: we already have this */
        }
    }

    if (s->flags & TCP_FIN) {
        if (!pcb->fin_seen) {
            pcb->fin_seq = s->seq + s->payload_len;
            pcb->fin_seen = true;
        }
        tcp_output(pcb, TCP_ACK, NULL, 0);
        consume_fin(slot, pcb);
    }
}

static void on_fin_wait_1(TcpPcb *pcb, const TcpSegment *s)
{
    if (seq_lt(pcb->snd_una, s->ack) && seq_leq(s->ack, pcb->snd_nxt)) {
        size_t delta = s->ack - pcb->snd_una;

        if (pcb->rtt_timing && seq_leq(pcb->rtt_seq, s->ack)) {
            uint32_t R = net_now_ms() - pcb->rtt_start;
            TcpRttUpdate(pcb, R);
            pcb->rtt_timing = false;
        }

        pcb->snd_una = s->ack;

        if (pcb->snd_nxt == pcb->snd_una) {
            pcb->buffered_bytes -= MIN(delta, pcb->buffered_bytes);
            rto_stop(pcb);
            LOG_INFO(LOG_TAG, "response acknowledged");
        }
    }
    bool our_fin_acked = seq_leq(pcb->snd_nxt, pcb->snd_una);

    if (s->flags & TCP_FIN) {
        /* their FIN arrived (with or without acking ours) */
        pcb->rcv_nxt = s->seq + 1;
        tcp_output(pcb, TCP_ACK, NULL, 0);
        pcb->state = TCP_TIME_WAIT;
        timer_arm(net_now_ms() + TCP_TIME_WAIT_MS, time_wait_cb, pcb);
    } else if (our_fin_acked) {
        pcb->state = TCP_FIN_WAIT_2;
    }
}

static void on_fin_wait_2(TcpPcb *pcb, const TcpSegment *s)
{
    if (s->flags & TCP_FIN) {
        pcb->rcv_nxt = s->seq + 1;
        tcp_output(pcb, TCP_ACK, NULL, 0);
        pcb->state = TCP_TIME_WAIT;
        timer_arm(net_now_ms() + TCP_TIME_WAIT_MS, time_wait_cb, pcb);
    }
}

static void on_last_ack(int slot, TcpPcb *pcb, const TcpSegment *s)
{
    if (seq_lt(pcb->snd_una, s->ack) && seq_leq(s->ack, pcb->snd_nxt)) {
        pcb->snd_una = s->ack;
        if (seq_leq(pcb->snd_nxt, pcb->snd_una)) { /* our FIN acked */
            rto_stop(pcb);
            port_release(pcb->local_port);
            tcp_pcb_free(slot);
            LOG_INFO(LOG_TAG, "LAST_ACK done, connection freed");
        }
    }
}

static void on_listening(TcpPcb *listener, const TcpSegment *s)
{
    if (!((s->flags & TCP_SYN) && !(s->flags & TCP_ACK)))
        return;

    int nidx = tcp_pcb_alloc();
    if (nidx < 0)
        return; /* no slot; todo: RST */
    TcpPcb *np = &tcp_pcbs[nidx];
    memset(np, 0, sizeof(*np));
    np->on_data = listener->on_data;
    np->active = true;
    np->local_ip = netif.ip;
    np->local_port = listener->local_port; /* same port we listen on */
    np->remote_ip = s->src_ip;
    np->remote_port = s->src_port;
    np->rcv_nxt = s->seq + 1; /* their SYN's phantom byte */
    np->rcv_rsq = np->rcv_nxt;
    np->snd_wnd = s->window;

    np->snd_nxt = netrand_u32(); /* our ISN */
    np->snd_una = np->snd_nxt;
    np->rto_ms = 1000;
    np->state = TCP_SYN_RCVD;
    tcp_output(np, TCP_SYN | TCP_ACK, NULL, 0); /* SYN-ACK */
    LOG_INFO(LOG_TAG, "SYN from %u.%u.%u.%u, now SYN_RCVD", IP4(s->src_ip));
}

static void on_syn_rcvd(TcpPcb *pcb, const TcpSegment *s)
{
    if (s->flags & TCP_ACK && s->ack == pcb->snd_nxt) {
        pcb->snd_una = s->ack;
        pcb->state = TCP_ESTABLISHED;
        LOG_INFO(LOG_TAG, "handshake complete, ESTABLISHED (server)");
    }
}

/* ------------------------------------------------------------------ */
/* entry points                                                       */
/* ------------------------------------------------------------------ */

static void tcp_dispatch(ipv4_addr_t src_ip, ipv4_addr_t dst_ip, const TcpSegment *seg)
{
    int slot = tcp_pcb_find(netif.ip, seg->dst_port, src_ip, seg->src_port);
    if (slot < 0) {
        if ((seg->flags & TCP_SYN) && !(seg->flags & TCP_ACK))
            slot = tcp_pcb_find_listener(netif.ip, seg->dst_port);
        if (slot < 0) {
            LOG_INFO(LOG_TAG, "no PCB: dst_port=%u netif.ip=%u.%u.%u.%u flags=0x%02x",
                     seg->dst_port, IP4(netif.ip), seg->flags);
            if (!(seg->flags & TCP_RST))
                tcp_send_rst(src_ip, dst_ip, seg);
            return;
        }
    }
    TcpPcb *pcb = &tcp_pcbs[slot];

    if (seg->flags & TCP_RST) {
        bool accept;
        if (pcb->state == TCP_SYN_SENT)
            accept = (seg->flags & TCP_ACK) && seg->ack == pcb->snd_nxt; /* RST must ack our SYN */
        else
            accept = (seg->seq == pcb->rcv_nxt); /* in-window */

        if (accept) {
            if (pcb->state == TCP_SYN_SENT)
                LOG_INFO(LOG_TAG, "connection refused");
            rto_stop(pcb);
            port_release(pcb->local_port);
            tcp_pcb_free(slot);
        }
        return;
    }

    pcb->snd_wnd = seg->window;

    switch (pcb->state) {
    case TCP_SYN_SENT:
        on_syn_sent(pcb, seg);
        break;
    case TCP_ESTABLISHED:
        on_established(slot, pcb, seg);
        break;
    case TCP_FIN_WAIT_1:
        on_fin_wait_1(pcb, seg);
        break;
    case TCP_FIN_WAIT_2:
        on_fin_wait_2(pcb, seg);
        break;
    case TCP_LAST_ACK:
        on_last_ack(slot, pcb, seg);
        break;
    case TCP_LISTENING:
        on_listening(pcb, seg);
        break;
    case TCP_SYN_RCVD:
        on_syn_rcvd(pcb, seg);
        break;
    default:
        break;
    }
}

void tcp_rx(ipv4_addr_t src_ip, ipv4_addr_t dst_ip, const uint8_t *data, uint16_t len)
{
    if (len < sizeof(TcpHdr))
        return;
    if (tcp_checksum(src_ip, dst_ip, data, len))
        return;

    LOG_DEBUG(LOG_TAG, "tcp_rx: len=%u", len);

    TcpHdr *th = (TcpHdr *)data;
    uint16_t hdr_len = (th->data_offset >> 4) * 4;

    if (hdr_len < 20 || hdr_len > len)
        return;

    TcpSegment seg = { .src_ip = src_ip,
                       .src_port = ntohs(th->src_port),
                       .dst_port = ntohs(th->dst_port),
                       .seq = ntohl(th->seq),
                       .ack = ntohl(th->ack),
                       .flags = th->flags,
                       .payload = data + hdr_len,
                       .payload_len = (uint16_t)(len - hdr_len),
                       .window = ntohs(th->window) };

    if (!tcp_parse_options(data + sizeof(TcpHdr), hdr_len - sizeof(TcpHdr), &seg)) {
        LOG_INFO(LOG_TAG, "malformed TCP options from %u.%u.%u.%u", IP4(src_ip));
    }

    /* TEST HOOK: deliver the first data segment in two halves, tail half
   * first, forcing the reordering no real sender will give us. The tail
   * lands past rcv_nxt (StoreOoo), the head then fills the hole
   * (FwdMerge). Remove after both log lines are seen. */
    static bool hook_done = false;
    if (!hook_done && seg.payload_len >= 2 && !(seg.flags & TCP_FIN)) {
        hook_done = true;
        uint16_t half = seg.payload_len / 2;
        TcpSegment tail = seg, head = seg;
        tail.seq += half;
        tail.payload += half;
        tail.payload_len -= half;
        uint16_t overlap = MIN(12, seg.payload_len - half);
        head.payload_len = half + overlap;
        LOG_INFO(LOG_TAG, "TEST: splitting seq=%u len=%u head=%u tail=%u overlap=%u", seg.seq,
                 seg.payload_len, head.payload_len, tail.payload_len, overlap);
        tcp_dispatch(src_ip, dst_ip, &tail);
        tcp_dispatch(src_ip, dst_ip, &head);
        return;
    }

    tcp_dispatch(src_ip, dst_ip, &seg);
}

int tcp_recv(int idx, uint8_t *buf, uint16_t sz)
{
    TcpPcb *pcb = &tcp_pcbs[idx];
    if (pcb->state != TCP_ESTABLISHED)
        return ERR_NOTCONN;

    size_t avail = pcb->rcv_nxt - pcb->rcv_rsq; // readable bytes
    size_t n = MIN(sz, avail);                  // what actually fits in caller's buf
    if (!n)
        return 0;

    size_t off = pcb->rcv_rsq & (TCP_RCV_BUF - 1); // offset in the RING (source)
    size_t first = MIN(n, TCP_RCV_BUF - off);      // bytes before ring wraps

    memcpy(buf, pcb->rcv_buf + off, first); // ring to caller, first chunk
    if (first < n)
        memcpy(buf + first, pcb->rcv_buf, n - first); // wrap, rest from ring start

    pcb->rcv_rsq += n; // twin of snd_una += delta, frees buffer space
    return n;
}

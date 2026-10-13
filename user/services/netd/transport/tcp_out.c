#include "tcp_out.h"
#include "../common/txframe.h"
#include "../link/eth.h"
#include "../net/ip.h"
#include "tcp_opts.h"
#include "tcp_pcb.h"
#include <convert.h>
#include <string.h>
#include <util/log.h>

uint16_t TcpChecksum(Ipv4Addr src_ip, Ipv4Addr dst_ip, const uint8_t *seg, uint16_t seg_len)
{
    uint8_t pseudo[12];
    memcpy(&pseudo[0], &src_ip, 4);
    memcpy(&pseudo[4], &dst_ip, 4);
    pseudo[8] = 0;
    pseudo[9] = IP_PROTO_TCP;
    pseudo[10] = (uint8_t)(seg_len >> 8);
    pseudo[11] = (uint8_t)(seg_len & 0xFF);

    uint32_t accum = InetCsumPartial(pseudo, sizeof(pseudo), 0);
    accum = InetCsumPartial(seg, seg_len, accum);
    return InetCsumFold(accum);
}

/* arm if not armed; set flag   */
void RtoStart(TcpPcb *pcb)
{
    if (pcb->rto_timer.armed)
        return;
    TimerArm(&pcb->rto_timer, NetNowMs() + pcb->rto_ms, TcpRtoCb, pcb);
}

/* cancel if armed; clear flag  */
void RtoStop(TcpPcb *pcb) { TimerCancel(&pcb->rto_timer); }

static void TcpCopyOut(const TcpPcb *pcb, uint32_t seq, uint8_t *dst, size_t len)
{
    size_t off = seq & (pcb->snd_size - 1);
    size_t first = MIN(len, pcb->snd_size - off);
    memcpy(dst, pcb->snd_buf + off, first);
    if (first < len)
        memcpy(dst + first, pcb->snd_buf, len - first);
}

int TcpOutput(TcpPcb *pcb, uint8_t flags, uint16_t data_len)
{
    uint8_t opts[TCP_MAX_OPTS];
    size_t optlen = 0;
    if (flags & TCP_SYN) {
        TcpOptsOut o = {.opts_present = TCP_OPT_MSS_BIT, .mss = TCP_MSS};
        optlen = TcpBuildOptions(opts, sizeof(opts), &o);
    }

    TxFrame f;
    int rc = TxFrameInit(&f, sizeof(EthHdr) + sizeof(IpHeader));
    if (rc != ZUZU_OK)
        return rc;

    TcpHdr *th = TxFrameAppend(&f, sizeof(TcpHdr) + optlen);
    uint8_t *payload = TxFrameAppend(&f, data_len);
    if (!th || !payload)
        return ERR_OVERFLOW;

    th->src_port = htons(pcb->local_port);
    th->dst_port = htons(pcb->remote_port);
    th->seq = htonl(pcb->snd_nxt);
    th->ack = (flags & TCP_ACK) ? htonl(pcb->rcv_nxt) : 0;
    th->flags = flags;

    uint32_t edge = pcb->rcv_rsq + pcb->rcv_size;
    if (SeqLt(pcb->rcv_adv, edge) && edge - pcb->rcv_adv < MIN(pcb->rcv_size / 2, pcb->snd_mss))
        edge = pcb->rcv_adv; /* receiver SWS: don't announce a tiny increase */
    uint16_t win = SeqLt(pcb->rcv_nxt, edge) ? edge - pcb->rcv_nxt : 0;
    pcb->rcv_adv = pcb->rcv_nxt + win;
    th->window = htons(win);
    // th->window = htons(4); // crippled window for test
    th->checksum = 0;
    th->urgent_ptr = 0;
    th->data_offset = (uint8_t)(((sizeof(TcpHdr) + optlen) / 4) << 4);

    if (optlen)
        memcpy((uint8_t *)th + sizeof(TcpHdr), opts, optlen);
    if (data_len)
        TcpCopyOut(pcb, pcb->snd_nxt, payload, data_len);

    uint16_t seg_len = sizeof(TcpHdr) + optlen + data_len;
    th->checksum = htons(TcpChecksum(pcb->local_ip, pcb->remote_ip, (const uint8_t *)th, seg_len));

    rc = IpSend(&f, pcb->local_ip, pcb->remote_ip, IP_PROTO_TCP);
    if (rc != ZUZU_OK)
        return rc;

    /* SYN and FIN each consume one sequence number; data consumes data_len. */
    pcb->snd_nxt += data_len + ((flags & TCP_SYN) ? 1 : 0) + ((flags & TCP_FIN) ? 1 : 0);
    if (SeqLt(pcb->snd_max, pcb->snd_nxt))
        pcb->snd_max = pcb->snd_nxt;

    return ZUZU_OK;
}

int TcpXmit(TcpPcb *pcb)
{
    bool sent = false;
    while (1) {

        uint32_t data_end = pcb->snd_una + pcb->buffered_bytes;
        if (SeqLeq(data_end, pcb->snd_nxt))
            break;
        size_t unsent = data_end - pcb->snd_nxt;
        size_t window_edge = pcb->snd_una + MIN(pcb->snd_wnd, pcb->cwnd);
        if (SeqLeq(window_edge, pcb->snd_nxt))
            break;

        size_t sendable = window_edge - pcb->snd_nxt;

        size_t seglen = MIN(MIN(unsent, sendable), pcb->snd_mss);

        bool retrans = SeqLt(pcb->snd_nxt, pcb->snd_max);
        bool full = seglen >= pcb->snd_mss;
        bool drains = seglen == unsent;
        bool idle = pcb->snd_max == pcb->snd_una;
        bool fin = pcb->fin_pending && drains;
        bool half = seglen >= pcb->max_snd_wnd / 2;

        if (!retrans && !(full || half || fin || (drains && idle)))
            break;

        if (seglen == 0)
            break;

        uint8_t flags = TCP_ACK;
        if (pcb->fin_pending && pcb->snd_nxt + seglen == pcb->snd_una + pcb->buffered_bytes) {
            flags |= TCP_FIN; /* this is the last data segment */
            pcb->fin_sent = true;
        }

        uint32_t seg_seq = pcb->snd_nxt; /* before TcpOutput advances it */
        int rc = TcpOutput(pcb, flags, seglen);
        if (rc != ZUZU_OK)
            return rc;
        sent = true;

        if (!pcb->rtt_timing) { /* only one measurement in flight */
            pcb->rtt_timing = true;
            pcb->rtt_start = NetNowMs();
            pcb->rtt_seq = seg_seq + seglen; /* ACK must pass the segment's end */
        }
    }
    uint32_t data_end = pcb->snd_una + pcb->buffered_bytes;
    bool fin_acked = pcb->fin_sent && SeqLeq(pcb->snd_max, pcb->snd_una);

    /* all data sent; emit the FIN alone if it hasn't gone out yet */
    if (pcb->fin_pending && !fin_acked && pcb->snd_nxt == data_end) {
        int rc = TcpOutput(pcb, TCP_FIN | TCP_ACK, 0);
        if (rc == ZUZU_OK) {
            pcb->fin_sent = true;
            sent = true;
        }
    }
    if (sent)
        RtoStart(pcb);

    bool blocked = SeqLt(pcb->snd_nxt, data_end) &&
                   SeqLeq(pcb->snd_una + pcb->snd_wnd, pcb->snd_nxt) &&
                   pcb->snd_max == pcb->snd_una;
    if (blocked) {
        if (!pcb->persist_timer.armed) {
            if (!pcb->persist_ms)
                pcb->persist_ms = pcb->rto_ms;
            TimerArm(&pcb->persist_timer, NetNowMs() + pcb->persist_ms, TcpPersistCb, pcb);
        }
    } else {
        TimerCancel(&pcb->persist_timer);
        pcb->persist_ms = 0;
    }

    return ZUZU_OK;
}

void TcpPersistCb(void *arg)
{
    TcpPcb *pcb = (TcpPcb *)arg;

    uint32_t data_end = pcb->snd_una + pcb->buffered_bytes;
    bool blocked =
        SeqLt(pcb->snd_nxt, data_end) && SeqLeq(pcb->snd_una + pcb->snd_wnd, pcb->snd_nxt);
    if (!blocked) {
        TcpXmit(pcb);
        return;
    }

    uint32_t saved = pcb->snd_nxt;
    pcb->snd_nxt = pcb->snd_una - 1;
    TcpOutput(pcb, TCP_ACK, 0);
    pcb->snd_nxt = saved;

    pcb->persist_ms *= 2;
    if (pcb->persist_ms > TCP_RTO_MAX)
        pcb->persist_ms = TCP_RTO_MAX;
    TimerArm(&pcb->persist_timer, NetNowMs() + pcb->persist_ms, TcpPersistCb, pcb);
}

void TcpRetransmitHead(TcpPcb *pcb)
{
    uint32_t saved = pcb->snd_nxt;
    uint32_t data_end = pcb->snd_una + pcb->buffered_bytes;
    size_t len = SeqLt(pcb->snd_una, data_end) ? MIN(pcb->snd_mss, data_end - pcb->snd_una) : 0;
    uint8_t flags = TCP_ACK;

    if (pcb->fin_sent && pcb->snd_una + len == data_end)
        flags |= TCP_FIN;

    pcb->snd_nxt = pcb->snd_una;
    TcpOutput(pcb, flags, len);
    pcb->snd_nxt = saved;
    pcb->rtt_timing = false;
}

void TcpRtoCb(void *arg)
{
    TcpPcb *pcb = (TcpPcb *)arg;
    if (pcb->snd_max == pcb->snd_una)
        return; // nothing outstanding, don't do anything

    if (++pcb->retries > (pcb->state == TCP_SYN_SENT || pcb->state == TCP_SYN_RCVD
                              ? TCP_SYN_RETRIES
                              : TCP_DATA_RETRIES)) {
        LOG_DEBUG(LOG_TAG, "retransmit limit reached, dropping connection");
        TcpPcbRelease(pcb, ERR_TIMEOUT);
        return;
    }

    /* exponential backoff, capped */
    pcb->rtt_timing = false; /* Karn: timed segment now ambiguous, drop the sample */
    pcb->rto_ms *= 2;
    if (pcb->rto_ms > TCP_RTO_MAX)
        pcb->rto_ms = TCP_RTO_MAX;

    LOG_DEBUG(LOG_TAG, "RTO fired: snd_nxt=%u snd_una=%u (rto now %u ms)", (uint32_t)pcb->snd_nxt,
             pcb->snd_una, pcb->rto_ms);

    pcb->snd_nxt = pcb->snd_una;

    if (pcb->state == TCP_SYN_SENT || pcb->state == TCP_SYN_RCVD) {
        TcpOutput(pcb, pcb->state == TCP_SYN_SENT ? TCP_SYN : TCP_SYN | TCP_ACK, 0);
        RtoStart(pcb);
        return;
    }

    uint32_t flight = pcb->snd_max - pcb->snd_una;
    pcb->ssthresh = MAX(flight / 2, 2U * pcb->snd_mss);
    pcb->cwnd = pcb->snd_mss;
    pcb->dupacks = 0;
    pcb->in_recovery = false;
    pcb->recover = pcb->snd_max;

    TcpXmit(pcb);
}

int TcpSend(int idx, const uint8_t *data, uint16_t len)
{
    TcpPcb *pcb = &tcp_pcbs[idx];
    if (pcb->state != TCP_ESTABLISHED && pcb->state != TCP_CLOSE_WAIT)
        return ERR_NOTCONN;
    if (!TcpSndBufAlloc(pcb))
        return ERR_NOMEM;
    size_t free = pcb->snd_size - pcb->buffered_bytes;
    if (free == 0) {
        pcb->snd_blocked = true;
        return 0; // backpressure
    }
    size_t n = MIN(len, free);
    if (n < len)
        pcb->snd_blocked = true;
    size_t off = (pcb->snd_una + pcb->buffered_bytes) & (pcb->snd_size - 1); // where the write starts
    size_t first = MIN(n, pcb->snd_size - off); // bytes before hitting the physical end
    memcpy(pcb->snd_buf + off, data, first);
    if (first < n) {
        memcpy(pcb->snd_buf, data + first, n - first);
    }
    pcb->buffered_bytes += n;
    TcpXmit(pcb);
    return n;
}

void TcpSendRst(Ipv4Addr src_ip, Ipv4Addr dst_ip, const TcpSegment *seg)
{
    uint8_t buf[sizeof(TcpHdr)]; /* bare header, no payload */
    TcpHdr *th = (TcpHdr *)buf;

    th->src_port = htons(seg->dst_port);
    th->dst_port = htons(seg->src_port);
    th->data_offset = (5 << 4); /* 20 bytes, no options */
    th->window = 0;             /* RST carries no window */
    th->urgent_ptr = 0;
    th->checksum = 0;

    if (seg->flags & TCP_ACK) {
        th->flags = TCP_RST;
        th->seq = htonl(seg->ack); /* sit where they expect */
        th->ack = 0;
    } else {
        uint32_t seg_len =
            seg->payload_len + ((seg->flags & TCP_SYN) ? 1 : 0) + ((seg->flags & TCP_FIN) ? 1 : 0);
        th->flags = TCP_RST | TCP_ACK;
        th->seq = 0;
        th->ack = htonl(seg->seq + seg_len); /* acknowledge their span */
    }

    th->checksum = htons(TcpChecksum(dst_ip, src_ip, buf, sizeof(buf)));
    IpTx(buf, sizeof(buf), dst_ip, src_ip, IP_PROTO_TCP);
}

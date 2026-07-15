#include "tcp.h"
#include "tcp_pcb.h"
#include "tcp_out.h"
#include "port.h"
#include "../common/netrand.h"
#include "../net/ip.h"
#include <convert.h>
#include <zuzu/log.h>
#include <string.h>

/* A received segment, parsed once and passed to the per-state handlers. */
typedef struct {
    ipv4_addr_t    src_ip;
    port_t         src_port;
    uint32_t       seq;          /* their sequence number */
    uint32_t       ack;          /* their acknowledgement number */
    uint8_t        flags;
    const uint8_t *payload;
    uint16_t       payload_len;
    uint16_t       window;
} tcp_seg_t;


/* ------------------------------------------------------------------ */
/* helpers                                                            */
/* ------------------------------------------------------------------ */

static void time_wait_cb(void *arg) {
    tcp_pcb_t *pcb = (tcp_pcb_t *)arg;
    timer_cancel(pcb->rto_timer);
    port_release(pcb->local_port);
    tcp_pcb_free(tcp_pcb_index(pcb));
    LOG_INFO(LOG_TAG, "TIME_WAIT expired, connection freed");
}

/* Copy in-order payload into the receive ring and advance rcv_nxt. */
static void deliver_data(tcp_pcb_t *pcb, const uint8_t *payload, uint16_t payload_len) {
    size_t used  = pcb->rcv_nxt - pcb->rcv_rsq;
    size_t free  = TCP_RCV_BUF - used;
    size_t n     = MIN(payload_len, free);
    size_t off   = pcb->rcv_nxt & (TCP_RCV_BUF - 1);
    size_t first = MIN(n, TCP_RCV_BUF - off);
    memcpy(pcb->rcv_buf + off, payload, first);
    if (first < n)
        memcpy(pcb->rcv_buf, payload + first, n - first);
    pcb->rcv_nxt += n;
}

/* ------------------------------------------------------------------ */
/* per-state handlers                                                 */
/* ------------------------------------------------------------------ */

static void on_syn_sent(tcp_pcb_t *pcb, const tcp_seg_t *s) {
    if (((s->flags & TCP_SYN) && (s->flags & TCP_ACK)) && s->ack == pcb->snd_nxt) {
        pcb->rcv_nxt = s->seq + 1;
        pcb->rcv_rsq = pcb->rcv_nxt;
        pcb->snd_una = s->ack;
        pcb->state = TCP_ESTABLISHED;
        tcp_output(pcb, TCP_ACK, NULL, 0);
    }
}

static void on_established(int slot, tcp_pcb_t *pcb, const tcp_seg_t *s) {
    if ((int32_t)(s->ack - pcb->snd_una) > 0) {
        size_t delta = s->ack - pcb->snd_una;   // how many bytes got confirmed
        pcb->snd_una = s->ack;
        pcb->buffered_bytes -= MIN(delta, pcb->buffered_bytes);
        if (pcb->snd_nxt == pcb->snd_una) {
            timer_cancel(pcb->rto_timer);
            LOG_INFO(LOG_TAG, "data acked, RTO cancelled");
        } else {
            timer_cancel(pcb->rto_timer);
            pcb->rto_timer = timer_arm(net_now_ms() + pcb->rto_ms, tcp_rto_cb, pcb);
            LOG_INFO(LOG_TAG, "partially acked, RTO restarted");
        }
    }

    if (s->payload_len && s->seq == pcb->rcv_nxt) {   /* in-order data */
        deliver_data(pcb, s->payload, s->payload_len);
        tcp_output(pcb, TCP_ACK, NULL, 0);            /* ack what we got */

        /* TODO: application logic wired directly into the transport.
         * This canned HTTP reply should move behind an app-layer callback. */
        static const char *resp =
            "HTTP/1.0 200 OK\r\n"
            "Content-Type: text/html\r\n"
            "Connection: close\r\n"
            "\r\n"
            "<html><body><h1>Hello from ZuzuOS!</h1>"
            "<p>Served by netd, powered by the Zuzu microkernel.</p>"
            "</body></html>\r\n";

        tcp_send(slot, (const uint8_t *)resp, strlen(resp));
        tcp_close(slot);                            /* close after responding */
    }

    if (s->flags & TCP_FIN) {
        pcb->rcv_nxt += 1;                    /* their FIN's phantom byte */
        tcp_output(pcb, TCP_ACK, NULL, 0);   /* ack their FIN */
        pcb->state = TCP_CLOSE_WAIT;
        LOG_INFO(LOG_TAG, "peer closed, now CLOSE_WAIT");
        tcp_close(slot);
    }
}

static void on_fin_wait_1(tcp_pcb_t *pcb, const tcp_seg_t *s) {
    if ((int32_t)(s->ack - pcb->snd_una) > 0) {
        size_t delta = s->ack - pcb->snd_una;
        pcb->snd_una = s->ack;

        if (pcb->snd_nxt == pcb->snd_una) {
            pcb->buffered_bytes -= MIN(delta, pcb->buffered_bytes);
            timer_cancel(pcb->rto_timer);
            LOG_INFO(LOG_TAG, "response acknowledged");
        }
    }
    bool our_fin_acked = ((int32_t)(pcb->snd_una - pcb->snd_nxt) >= 0);

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

static void on_fin_wait_2(tcp_pcb_t *pcb, const tcp_seg_t *s) {
    if (s->flags & TCP_FIN) {
        pcb->rcv_nxt = s->seq + 1;
        tcp_output(pcb, TCP_ACK, NULL, 0);
        pcb->state = TCP_TIME_WAIT;
        timer_arm(net_now_ms() + TCP_TIME_WAIT_MS, time_wait_cb, pcb);
    }
}

static void on_last_ack(int slot, tcp_pcb_t *pcb, const tcp_seg_t *s) {
    if ((int32_t)(s->ack - pcb->snd_una) > 0) {
        pcb->snd_una = s->ack;
        if ((int32_t)(pcb->snd_una - pcb->snd_nxt) >= 0) {   /* our FIN acked */
            timer_cancel(pcb->rto_timer);
            port_release(pcb->local_port);
            tcp_pcb_free(slot);
            LOG_INFO(LOG_TAG, "LAST_ACK done, connection freed");
        }
    }
}

static void on_listening(tcp_pcb_t *listener, const tcp_seg_t *s) {
    if (!((s->flags & TCP_SYN) && !(s->flags & TCP_ACK))) return;

    int nidx = tcp_pcb_alloc();
    if (nidx < 0) return;              /* no slot; todo: RST */
    tcp_pcb_t *np = &tcp_pcbs[nidx];
    memset(np, 0, sizeof(*np));
    np->active = true;
    np->local_ip = netif.ip;
    np->local_port = listener->local_port;   /* same port we listen on */
    np->remote_ip = s->src_ip;
    np->remote_port = s->src_port;
    np->rcv_nxt = s->seq + 1;                 /* their SYN's phantom byte */
    np->rcv_rsq = np->rcv_nxt;
    np->snd_wnd = ntohs(s->window);
    
    np->snd_nxt = netrand_u32();              /* our ISN */
    np->snd_una = np->snd_nxt;
    np->rto_ms = 1000;
    np->state = TCP_SYN_RCVD;
    tcp_output(np, TCP_SYN | TCP_ACK, NULL, 0);   /* SYN-ACK */
    LOG_INFO(LOG_TAG, "SYN from %u.%u.%u.%u, now SYN_RCVD", IP4(s->src_ip));
}

static void on_syn_rcvd(tcp_pcb_t *pcb, const tcp_seg_t *s) {
    if (s->flags & TCP_ACK && s->ack == pcb->snd_nxt) {
        pcb->snd_una = s->ack;
        pcb->state = TCP_ESTABLISHED;
        LOG_INFO(LOG_TAG, "handshake complete, ESTABLISHED (server)");
    }
}

/* ------------------------------------------------------------------ */
/* entry points                                                       */
/* ------------------------------------------------------------------ */

void tcp_rx(ipv4_addr_t src_ip, ipv4_addr_t dst_ip, const uint8_t *data, uint16_t len) {
    if (len < sizeof(tcp_hdr_t)) return;
    if (tcp_checksum(src_ip, dst_ip, data, len)) return;

    tcp_hdr_t *th = (tcp_hdr_t *)data;
    uint16_t hdr_len = (th->data_offset >> 4) * 4;

    tcp_seg_t seg = {
        .src_ip      = src_ip,
        .src_port    = ntohs(th->src_port),
        .seq         = ntohl(th->seq),
        .ack         = ntohl(th->ack),
        .flags       = th->flags,
        .payload     = data + hdr_len,
        .payload_len = (uint16_t)(len - hdr_len),
        .window = ntohs(th->window)
    };

    int slot = tcp_pcb_find(netif.ip, ntohs(th->dst_port), src_ip, seg.src_port);
    if (slot < 0) {
        if ((seg.flags & TCP_SYN) && !(seg.flags & TCP_ACK))
            slot = tcp_pcb_find_listener(netif.ip, ntohs(th->dst_port));
        if (slot < 0) return;   /* todo: RST */
    }
    tcp_pcb_t *pcb = &tcp_pcbs[slot];
    pcb->snd_wnd = seg.window;

    switch (pcb->state) {
        case TCP_SYN_SENT:    on_syn_sent(pcb, &seg);          break;
        case TCP_ESTABLISHED: on_established(slot, pcb, &seg); break;
        case TCP_FIN_WAIT_1:  on_fin_wait_1(pcb, &seg);        break;
        case TCP_FIN_WAIT_2:  on_fin_wait_2(pcb, &seg);        break;
        case TCP_LAST_ACK:    on_last_ack(slot, pcb, &seg);    break;
        case TCP_LISTENING:   on_listening(pcb, &seg);         break;
        case TCP_SYN_RCVD:    on_syn_rcvd(pcb, &seg);          break;
        default: break;
    }
}

int tcp_recv(int idx, uint8_t *buf, uint16_t sz) {
    tcp_pcb_t *pcb = &tcp_pcbs[idx];
    if (pcb->state != TCP_ESTABLISHED) return ERR_NOTCONN;

    size_t avail = pcb->rcv_nxt - pcb->rcv_rsq;   // readable bytes
    size_t n = MIN(sz, avail);                    // what actually fits in caller's buf
    if (!n) return 0;

    size_t off   = pcb->rcv_rsq & (TCP_RCV_BUF - 1);   // offset in the RING (source)
    size_t first = MIN(n, TCP_RCV_BUF - off);          // bytes before ring wraps

    memcpy(buf, pcb->rcv_buf + off, first);            // ring to caller, first chunk
    if (first < n)
        memcpy(buf + first, pcb->rcv_buf, n - first);  // wrap, rest from ring start

    pcb->rcv_rsq += n;   // twin of snd_una += delta, frees buffer space
    return n;
}

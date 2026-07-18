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
/* ------------------------------------------------------------------ */
/* helpers                                                            */
/* ------------------------------------------------------------------ */

static void time_wait_cb(void *arg) {
    tcp_pcb_t *pcb = (tcp_pcb_t *)arg;
    rto_stop(pcb);
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

static void store_ooo(tcp_pcb_t *pcb, const tcp_seg_t *s) {
    /* case 2: future data (hole before it)  */
    uint32_t reach = s->seq + s->payload_len - pcb->rcv_rsq;
    if (reach > TCP_RCV_BUF) {
        LOG_INFO(LOG_TAG, "OOO seg beyond window, dropping: reach=%u", reach);
        return;                    /* peer will retransmit */
    } 
    if (pcb->ooo_count == 0) {
        pcb->ooo[0].start = s->seq;
        pcb->ooo[0].end   = s->seq + s->payload_len;
        pcb->ooo_count    = 1;
    } else {
        LOG_INFO(LOG_TAG, "second OOO range, dropping (no merge yet)");
        return;
    }
    size_t off   = s->seq & (TCP_RCV_BUF - 1);
    size_t first = MIN(s->payload_len, TCP_RCV_BUF - off);
    memcpy(pcb->rcv_buf + off, s->payload, first);
    if (first < s->payload_len)
        memcpy(pcb->rcv_buf, s->payload + first, s->payload_len - first);

}

/* After rcv_nxt advances, absorb any OOO range it now connects with. */
static void forward_merge(tcp_pcb_t *pcb) {
    if (pcb->ooo_count == 0) return;

    /* does rcv_nxt now fall inside (or at the start of) the stored range? */
    if ((int32_t)(pcb->rcv_nxt - pcb->ooo[0].start) >= 0 &&
        (int32_t)(pcb->ooo[0].end - pcb->rcv_nxt) > 0) {
        pcb->rcv_nxt = pcb->ooo[0].end;
        pcb->ooo_count = 0;
        LOG_INFO(LOG_TAG, "hole closed, rcv_nxt advanced to %u", pcb->rcv_nxt);
    }
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
            rto_stop(pcb);
            LOG_INFO(LOG_TAG, "data acked, RTO cancelled");
        } else {
            rto_stop(pcb);
            rto_start(pcb);
            LOG_INFO(LOG_TAG, "partially acked, RTO restarted");
        }
    }

    if (s->payload_len) {
        if (s->seq == pcb->rcv_nxt) {
            deliver_data(pcb, s->payload, s->payload_len);
            forward_merge(pcb);
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

            // tcp_send(slot, (const uint8_t *)resp, strlen(resp));
            // tcp_close(slot);                            /* close after responding */
        } else if ((int32_t)(s->seq - pcb->rcv_nxt) > 0) {
            store_ooo(pcb, s);
            LOG_INFO(LOG_TAG, "OOO seg: seq=%u rcv_nxt=%u len=%u",
                    s->seq, pcb->rcv_nxt, s->payload_len);
            tcp_output(pcb, TCP_ACK, NULL, 0);      /* dup-ACK: still want rcv_nxt */
        } else {
            /* case 3: old duplicate */
            LOG_INFO(LOG_TAG, "dup seg: seq=%u rcv_nxt=%u len=%u",
                    s->seq, pcb->rcv_nxt, s->payload_len);
            tcp_output(pcb, TCP_ACK, NULL, 0);      /* re-ACK: we already have this */
        }
    }
    
    if (s->flags & TCP_FIN) {
        pcb->rcv_nxt += 1; /* todo: fix this (may break OOO)*/
        tcp_output(pcb, TCP_ACK, NULL, 0);
        if (pcb->state == TCP_ESTABLISHED) {
            pcb->state = TCP_CLOSE_WAIT;
            tcp_close(slot);
        } else if (pcb->state == TCP_FIN_WAIT_1) {
            pcb->state = TCP_TIME_WAIT;
            timer_arm(net_now_ms() + TCP_TIME_WAIT_MS, time_wait_cb, pcb);
        }
    }
}

static void on_fin_wait_1(tcp_pcb_t *pcb, const tcp_seg_t *s) {
    if ((int32_t)(s->ack - pcb->snd_una) > 0) {
        size_t delta = s->ack - pcb->snd_una;
        pcb->snd_una = s->ack;

        if (pcb->snd_nxt == pcb->snd_una) {
            pcb->buffered_bytes -= MIN(delta, pcb->buffered_bytes);
            rto_stop(pcb);
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
            rto_stop(pcb);
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

    LOG_INFO(LOG_TAG, "tcp_rx: len=%u", len);

    tcp_hdr_t *th = (tcp_hdr_t *)data;
    uint16_t hdr_len = (th->data_offset >> 4) * 4;

    if (hdr_len < 20 || hdr_len > len) return;

    tcp_seg_t seg = {
        .src_ip      = src_ip,
        .src_port    = ntohs(th->src_port),
        .dst_port    = ntohs(th->dst_port),
        .seq         = ntohl(th->seq),
        .ack         = ntohl(th->ack),
        .flags       = th->flags,
        .payload     = data + hdr_len,
        .payload_len = (uint16_t)(len - hdr_len),
        .window = ntohs(th->window)
    };

    /* TEST HOOK: drop the 2nd data segment once, forcing a hole */
    static int drop_ctr = 0;
    if (seg.payload_len && ++drop_ctr == 2) {
        LOG_INFO(LOG_TAG, "TEST: dropping segment seq=%u", seg.seq);
        return;
    }

    int slot = tcp_pcb_find(netif.ip, ntohs(th->dst_port), src_ip, seg.src_port);
    if (slot < 0) {
        if ((seg.flags & TCP_SYN) && !(seg.flags & TCP_ACK))
            slot = tcp_pcb_find_listener(netif.ip, ntohs(th->dst_port));
        if (slot < 0) {
            LOG_INFO(LOG_TAG, "no PCB: dst_port=%u netif.ip=%u.%u.%u.%u flags=0x%02x",
                     seg.dst_port, IP4(netif.ip), seg.flags);
            if (!(seg.flags & TCP_RST))
                tcp_send_rst(src_ip, dst_ip, &seg);
            return;
        }
    }
    tcp_pcb_t *pcb = &tcp_pcbs[slot];

    if (seg.flags & TCP_RST) {
        bool accept;
        if (pcb->state == TCP_SYN_SENT)
            accept = (seg.flags & TCP_ACK) && seg.ack == pcb->snd_nxt;  /* RST must ack our SYN */
        else
            accept = (seg.seq == pcb->rcv_nxt);                          /* in-window */

        if (accept) {
            if (pcb->state == TCP_SYN_SENT)
                LOG_INFO(LOG_TAG, "connection refused");
            rto_stop(pcb);
            port_release(pcb->local_port);
            tcp_pcb_free(slot);
        }
        return;
    }

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

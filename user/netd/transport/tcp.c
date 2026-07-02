#include "tcp.h"
#include "../app/dhcp.h"
#include "port.h"
#include "../common/netrand.h"
#include "../net/ip.h"
#include "../link/eth.h"
#include "../common/txframe.h"
#include <zuzu/log.h>
#include <string.h>

static tcp_pcb_t pcbs[TCP_MAX_PCB];

static int pcb_alloc(void) {
    for (int i = 0; i < TCP_MAX_PCB; i++) {
        if (!pcbs[i].active) {
            pcbs[i].active = true;
            return i;
        }
    }

    return ERR_NOMEM;
}

static void pcb_free(int h) {
    if (h < 0 || h > TCP_MAX_PCB - 1) return;
    pcbs[h].active = false;
}

static int pcb_find(ipv4_addr_t local_ip, port_t local_port, ipv4_addr_t remote_ip, port_t remote_port) {
    // exact match
    for (int i = 0; i < TCP_MAX_PCB; i++) {
        if (pcbs[i].local_ip == local_ip &&
            pcbs[i].local_port == local_port &&
            pcbs[i].remote_ip == remote_ip &&
        pcbs[i].remote_port == remote_port) {
            return i;
        }
    }
    return ERR_NOMATCH;
} 

static int pcb_find_listener(ipv4_addr_t local_ip, port_t local_port) {
    for (int i = 0; i < TCP_MAX_PCB; i++) {
        if (pcbs[i].active &&
            pcbs[i].state == TCP_LISTENING &&    
            pcbs[i].local_ip == local_ip &&
            pcbs[i].local_port == local_port) {
            return i;
        }
    }
    return ERR_NOMATCH;
}

static uint16_t tcp_checksum(ipv4_addr_t src_ip, ipv4_addr_t dst_ip,
                             const uint8_t *seg, uint16_t seg_len) {
    uint8_t pseudo[12];
    memcpy(&pseudo[0], &src_ip, 4);
    memcpy(&pseudo[4], &dst_ip, 4);
    pseudo[8]  = 0;
    pseudo[9]  = IP_PROTO_TCP;
    pseudo[10] = (uint8_t)(seg_len >> 8);
    pseudo[11] = (uint8_t)(seg_len & 0xFF);

    uint32_t accum = inet_csum_partial(pseudo, sizeof(pseudo), 0);
    accum = inet_csum_partial(seg, seg_len, accum);
    return inet_csum_fold(accum);
}

static int tcp_output(tcp_pcb_t *pcb, uint8_t flags, const uint8_t *data, uint16_t data_len);
static int tcp_xmit(tcp_pcb_t *pcb);

void rto_cb(void *arg) {
    tcp_pcb_t *pcb = (tcp_pcb_t *)arg;
    if (pcb->snd_nxt == pcb->snd_una) return; // window empty, don't do anything

    /* exponential backoff, capped */
    pcb->rto_ms *= 2; // todo: RTT estimation later
    if (pcb->rto_ms > TCP_RTO_MAX) pcb->rto_ms = TCP_RTO_MAX;

    LOG_INFO(LOG_TAG, "RTO fired: snd_nxt=%u snd_una=%u (rto now %u ms)",
             (uint32_t)pcb->snd_nxt, pcb->snd_una, pcb->rto_ms);

    pcb->snd_nxt = pcb->snd_una;
    tcp_xmit(pcb);

    pcb->rto_timer = timer_arm(net_now_ms() + pcb->rto_ms, rto_cb, pcb);
}

static int tcp_output(tcp_pcb_t *pcb, uint8_t flags, const uint8_t *data, uint16_t data_len) {
    uint8_t buf[sizeof(tcp_hdr_t) + TCP_MSS];      /* header + bit of data */
    tcp_hdr_t *th = (tcp_hdr_t *)buf;

    th->src_port   = htons(pcb->local_port);
    th->dst_port   = htons(pcb->remote_port);
    th->seq        = htonl(pcb->snd_nxt);
    th->ack        = (flags & TCP_ACK) ? htonl(pcb->rcv_nxt) : 0;
    th->data_offset = (5 << 4);          /* 20-byte header, no options */
    th->flags      = flags;
    th->window     = htons(TCP_DEFAULT_WINDOW);
    th->checksum   = 0;
    th->urgent_ptr = 0;

    if (data_len)
        memcpy(buf + sizeof(tcp_hdr_t), data, data_len);

    uint16_t seg_len = sizeof(tcp_hdr_t) + data_len;   /* + data_len when you add data */
    th->checksum = htons(tcp_checksum(pcb->local_ip, pcb->remote_ip, buf, seg_len));

    int rc = ip_tx(buf, seg_len, pcb->local_ip, pcb->remote_ip, IP_PROTO_TCP);
    LOG_INFO(LOG_TAG, "ip_tx rc=%d", rc);
    if (rc != ZUZU_OK)
        return rc;

    /* SYN and FIN each consume one sequence number; data consumes data_len. */
    pcb->snd_nxt += data_len + ((flags & TCP_SYN) ? 1 : 0) + ((flags & TCP_FIN) ? 1 : 0);


    return ZUZU_OK;
}

static void time_wait_cb(void *arg) {
    tcp_pcb_t *pcb = (tcp_pcb_t *)arg;
    int idx = pcb - pcbs;              /* pointer → index */
    timer_cancel(pcb->rto_timer);
    port_release(pcb->local_port);
    pcb_free(idx);
    LOG_INFO(LOG_TAG, "TIME_WAIT expired, connection freed");
}

static int tcp_xmit(tcp_pcb_t *pcb) {
    size_t in_flight = pcb->snd_nxt - pcb->snd_una;
    bool sent = false;
    while (1) {   
        size_t unsent = (pcb->snd_una + pcb->buffered_bytes) - pcb->snd_nxt;
        if (!unsent) break;
        sent = true;
        size_t seglen = MIN(unsent, TCP_MSS);
        uint8_t data[TCP_MSS];
        size_t off = pcb->snd_nxt & (TCP_SND_BUF - 1);
        size_t first = MIN(seglen, TCP_SND_BUF - off);                     
        memcpy(data, pcb->snd_buf + off, first);
        if (first < seglen) {
            memcpy(data + first, pcb->snd_buf, seglen - first);
        }
        int rc = tcp_output(pcb, TCP_ACK, data, seglen);
        if (rc != ZUZU_OK) {
            return rc;
        }
    }
    if (!in_flight && sent) {
        pcb->rto_timer = timer_arm(net_now_ms() + pcb->rto_ms, rto_cb, pcb);
    }
    return ZUZU_OK;
}


int tcp_send(int idx, const uint8_t *data, uint16_t len) {
    tcp_pcb_t *pcb = &pcbs[idx];
    if (pcb->state != TCP_ESTABLISHED) return ERR_SYSDOWN;
    LOG_INFO(LOG_TAG, "Buffered bytes: %u", pcb->buffered_bytes);
    size_t free = TCP_SND_BUF - pcb->buffered_bytes;
    if (free == 0) return 0; // backpressure
    size_t n = MIN(len, free);
    size_t off   = (pcb->snd_una + pcb->buffered_bytes) & (TCP_SND_BUF - 1);   // where the write starts
    size_t first = MIN(n, TCP_SND_BUF - off);                                  // bytes before hitting the physical end
    memcpy(pcb->snd_buf + off, data, first);
    if (first < n) {
        memcpy(pcb->snd_buf, data + first, n - first);
    }
    pcb->buffered_bytes += n;
    tcp_xmit(pcb);
    return n;
}

int tcp_connect(ipv4_addr_t remote_ip, port_t remote_port) {
    if (!dhcp_is_bound()) return ERR_SYSDOWN;
    int idx = pcb_alloc();
    if (idx < 0) return ERR_NOMEM;
    tcp_pcb_t *pcb = &pcbs[idx];
    pcb->remote_ip = remote_ip;
    pcb->remote_port = remote_port;
    pcb->local_ip = netif.ip;
    pcb->local_port = port_alloc();
    if (pcb->local_port == 0) { pcb_free(idx); return ERR_NOMEM; }
    pcb->snd_nxt = netrand_u32();
    pcb->snd_una = pcb->snd_nxt;
    pcb->rcv_nxt = 0;
    pcb->rcv_rsq = pcb->rcv_nxt;
    pcb->rto_ms = 1000;
    pcb->state = TCP_SYN_SENT;
    int rc = tcp_output(pcb, TCP_SYN, NULL, 0);
    if (rc != ZUZU_OK) { port_release(pcb->local_port); pcb_free(idx); return rc; }
    LOG_INFO(LOG_TAG, "SYN -> %u.%u.%u.%u:%u", IP4(remote_ip), remote_port);
    return idx;
}

void tcp_rx(ipv4_addr_t src_ip, ipv4_addr_t dst_ip, const uint8_t *data, uint16_t len) {
    if (len < sizeof(tcp_hdr_t)) return;
    if (tcp_checksum(src_ip, dst_ip, data, len)) return;

    tcp_hdr_t *th = (tcp_hdr_t *)data;
    uint32_t their_seq = ntohl(th->seq);
    uint32_t ack = ntohl(th->ack);
    uint8_t flags = th->flags;

    int slot = pcb_find(netif.ip, ntohs(th->dst_port), src_ip, ntohs(th->src_port));
    if (slot < 0) {
        if ((flags & TCP_SYN) && !(flags & TCP_ACK))
            slot = pcb_find_listener(netif.ip, ntohs(th->dst_port));
        if (slot < 0) return;   /* todo: RST */
    }
    tcp_pcb_t *pcb = &pcbs[slot];

    switch(pcb->state) {
        case TCP_SYN_SENT: {
            if (((flags & TCP_SYN) && (flags & TCP_ACK)) && ack == pcb->snd_nxt) {
                pcb->rcv_nxt = their_seq + 1;
                pcb->rcv_rsq = pcb->rcv_nxt;
                pcb->snd_una = ack;
                pcb->state = TCP_ESTABLISHED;
                tcp_output(pcb, TCP_ACK, NULL, 0);

                /*
                const char *req =
                    "GET / HTTP/1.0\r\n"
                    "Host: www.google.com\r\n"
                    "User-Agent: ZuzuOS/1.0 (Scottish Fold; netd)\r\n"
                    "\r\n";
                tcp_send(slot, (const uint8_t *)req, strlen(req));*/
            }
        } break;
        case TCP_ESTABLISHED: {
            uint16_t hdr_len = (th->data_offset >> 4) * 4;
            const uint8_t *payload = data + hdr_len;
            uint16_t payload_len = len - hdr_len;

            if ((int32_t)(ack - pcb->snd_una) > 0) {
                size_t delta = ack - pcb->snd_una;   // how many bytes got confirmed
                pcb->snd_una = ack;
                pcb->buffered_bytes -= MIN(delta, pcb->buffered_bytes);
                if (pcb->snd_nxt == pcb->snd_una) {
                    timer_cancel(pcb->rto_timer);
                    LOG_INFO(LOG_TAG, "data acked, RTO cancelled");
                } else {
                    timer_cancel(pcb->rto_timer);
                    pcb->rto_timer = timer_arm(net_now_ms() + pcb->rto_ms, rto_cb, pcb);
                    LOG_INFO(LOG_TAG, "partially acked, RTO restarted");
                }
            }

            if (payload_len && their_seq == pcb->rcv_nxt) {   /* in-order data */
                size_t used = pcb->rcv_nxt - pcb->rcv_rsq;
                size_t free = TCP_RCV_BUF - used;
                size_t n = MIN(payload_len, free);
                size_t off = pcb->rcv_nxt & (TCP_RCV_BUF - 1);
                size_t first = MIN(n, TCP_RCV_BUF - off);
                memcpy(pcb->rcv_buf + off, payload, first);
                if (first < n)
                    memcpy(pcb->rcv_buf, payload + first, n - first);
                pcb->rcv_nxt += n;
                tcp_output(pcb, TCP_ACK, NULL, 0);            /* ack what we got */
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

            if (flags & TCP_FIN) {
                pcb->rcv_nxt += 1;        /* their FIN's phantom byte */
                tcp_output(pcb, TCP_ACK, NULL, 0);   /* ack their FIN */
                pcb->state = TCP_CLOSE_WAIT;
                LOG_INFO(LOG_TAG, "peer closed, now CLOSE_WAIT");
                tcp_close(slot);
            }
        } break;
        case TCP_FIN_WAIT_1: {
            if ((int32_t)(ack - pcb->snd_una) > 0) {
                size_t delta = ack - pcb->snd_una;
                pcb->snd_una = ack;  
            
                if (pcb->snd_nxt == pcb->snd_una) {
                    pcb->buffered_bytes -= MIN(delta, pcb->buffered_bytes);
                    timer_cancel(pcb->rto_timer);
                    LOG_INFO(LOG_TAG, "response acknowledged");
                }
            }
            bool our_fin_acked = ((int32_t)(pcb->snd_una - pcb->snd_nxt) >= 0);

            if (flags & TCP_FIN) {
                /* their FIN arrived (with or without acking ours) */
                pcb->rcv_nxt = their_seq + 1;
                tcp_output(pcb, TCP_ACK, NULL, 0);
                pcb->state = TCP_TIME_WAIT;
                timer_arm(net_now_ms() + TCP_TIME_WAIT_MS, time_wait_cb, pcb);
            } else if (our_fin_acked) {
                pcb->state = TCP_FIN_WAIT_2;
            }
        } break;
        case TCP_FIN_WAIT_2: {
            if (flags & TCP_FIN) {
                pcb->rcv_nxt = their_seq + 1;
                tcp_output(pcb, TCP_ACK, NULL, 0);
                pcb->state = TCP_TIME_WAIT;
                timer_arm(net_now_ms() + TCP_TIME_WAIT_MS, time_wait_cb, pcb);
            }
        } break;
        case TCP_LAST_ACK: {
            if ((int32_t)(ack - pcb->snd_una) > 0) {
                pcb->snd_una = ack;
                if ((int32_t)(pcb->snd_una - pcb->snd_nxt) >= 0) {   /* our FIN acked */
                    timer_cancel(pcb->rto_timer);
                    port_release(pcb->local_port);
                    pcb_free(slot);
                    LOG_INFO(LOG_TAG, "LAST_ACK done, connection freed");
                }
            }
        } break;
        case TCP_LISTENING: {
            if ((flags & TCP_SYN) && !(flags & TCP_ACK)) {
                int nidx = pcb_alloc();
                if (nidx < 0) return;              /* no slot; todo: RST */
                tcp_pcb_t *np = &pcbs[nidx];
                memset(np, 0, sizeof(*np));
                np->active = true;
                np->local_ip = netif.ip;
                np->local_port = pcb->local_port;   /* same port we listen on */
                np->remote_ip = src_ip;
                np->remote_port = ntohs(th->src_port);
                np->rcv_nxt = their_seq + 1;        /* their SYN's phantom byte */
                np->rcv_rsq = np->rcv_nxt;
                np->snd_nxt = netrand_u32();        /* our ISN */
                np->snd_una = np->snd_nxt;
                np->rto_ms = 1000;
                np->state = TCP_SYN_RCVD;
                tcp_output(np, TCP_SYN | TCP_ACK, NULL, 0);   /* SYN-ACK */
                LOG_INFO(LOG_TAG, "SYN from %u.%u.%u.%u, now SYN_RCVD", IP4(src_ip));
            }
        } break;
        case TCP_SYN_RCVD: {
            if (flags & TCP_ACK && ack == pcb->snd_nxt) {
                pcb->snd_una = ack;
                pcb->state = TCP_ESTABLISHED;
                LOG_INFO(LOG_TAG, "handshake complete, ESTABLISHED (server)");
            }
        } break;
    }
}

int tcp_listen(int port) {
    int slot = pcb_alloc();
    if (slot < 0) return ERR_NOMEM;
    tcp_pcb_t *pcb = &pcbs[slot];
    memset(pcb, 0, sizeof(*pcb));     /* clear stale state from prior use */
    pcb->active = true;               /* memset cleared it, set it back */
    pcb->local_ip = netif.ip;
    pcb->local_port = port;
    pcb->state = TCP_LISTENING;
    return slot;
}

int tcp_close(int idx) {
    tcp_pcb_t *pcb = &pcbs[idx];
    int rc = tcp_output(pcb, TCP_FIN | TCP_ACK, NULL, 0);
    if (rc == 0) {
        if (pcb->state == TCP_ESTABLISHED)
            pcb->state = TCP_FIN_WAIT_1;      /* active close */
        else if (pcb->state == TCP_CLOSE_WAIT)
            pcb->state = TCP_LAST_ACK;        /* passive close */
    }
    return rc;
}
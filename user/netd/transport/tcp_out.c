#include "tcp_out.h"
#include "tcp_pcb.h"
#include "../net/ip.h"
#include "../common/txframe.h"
#include <convert.h>
#include <zuzu/log.h>
#include <string.h>

uint16_t tcp_checksum(ipv4_addr_t src_ip, ipv4_addr_t dst_ip,
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

int tcp_output(tcp_pcb_t *pcb, uint8_t flags, const uint8_t *data, uint16_t data_len) {
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

    uint16_t seg_len = sizeof(tcp_hdr_t) + data_len;
    th->checksum = htons(tcp_checksum(pcb->local_ip, pcb->remote_ip, buf, seg_len));

    int rc = ip_tx(buf, seg_len, pcb->local_ip, pcb->remote_ip, IP_PROTO_TCP);
    LOG_INFO(LOG_TAG, "ip_tx rc=%d", rc);
    if (rc != ZUZU_OK)
        return rc;

    /* SYN and FIN each consume one sequence number; data consumes data_len. */
    pcb->snd_nxt += data_len + ((flags & TCP_SYN) ? 1 : 0) + ((flags & TCP_FIN) ? 1 : 0);

    return ZUZU_OK;
}

int tcp_xmit(tcp_pcb_t *pcb) {
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
        pcb->rto_timer = timer_arm(net_now_ms() + pcb->rto_ms, tcp_rto_cb, pcb);
    }
    return ZUZU_OK;
}

void tcp_rto_cb(void *arg) {
    tcp_pcb_t *pcb = (tcp_pcb_t *)arg;
    if (pcb->snd_nxt == pcb->snd_una) return; // window empty, don't do anything

    /* exponential backoff, capped */
    pcb->rto_ms *= 2; // todo: RTT estimation later
    if (pcb->rto_ms > TCP_RTO_MAX) pcb->rto_ms = TCP_RTO_MAX;

    LOG_INFO(LOG_TAG, "RTO fired: snd_nxt=%u snd_una=%u (rto now %u ms)",
             (uint32_t)pcb->snd_nxt, pcb->snd_una, pcb->rto_ms);

    pcb->snd_nxt = pcb->snd_una;
    tcp_xmit(pcb);

    pcb->rto_timer = timer_arm(net_now_ms() + pcb->rto_ms, tcp_rto_cb, pcb);
}

int tcp_send(int idx, const uint8_t *data, uint16_t len) {
    tcp_pcb_t *pcb = &tcp_pcbs[idx];
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

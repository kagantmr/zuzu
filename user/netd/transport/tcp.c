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

static int tcp_output(tcp_pcb_t *pcb, uint8_t flags, const uint8_t *data, uint16_t data_len) {
    uint8_t buf[sizeof(tcp_hdr_t) + 256];      /* header + bit of data */
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
    pcb->snd_nxt += ((flags & TCP_SYN) ? 1 : 0) + ((flags & TCP_FIN) ? 1 : 0);

    return ZUZU_OK;
}

int tcp_send(int idx, const uint8_t *data, uint16_t len) {
    tcp_pcb_t *pcb = &pcbs[idx];
    if (pcb->state != TCP_ESTABLISHED) return ERR_SYSDOWN;
    return tcp_output(pcb, TCP_ACK, data, len);
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
    int slot = pcb_find(netif.ip, ntohs(th->dst_port), src_ip, ntohs(th->src_port));
    if (slot < 0) return; //todo: listener check or rst 
    tcp_pcb_t *pcb = &pcbs[slot];
    uint32_t their_seq = ntohl(th->seq);
    uint32_t ack = ntohl(th->ack);
    uint8_t flags = th->flags;

    switch(pcb->state) {
        case TCP_SYN_SENT: {
            if (((flags & TCP_SYN) && (flags & TCP_ACK)) && ack == pcb->snd_nxt) {
                pcb->rcv_nxt = their_seq + 1;
                pcb->snd_una = ack;
                pcb->state = TCP_ESTABLISHED;
                tcp_output(pcb, TCP_ACK, NULL, 0);

                const char *req =
                    "GET / HTTP/1.0\r\n"
                    "Host: www.google.com\r\n"
                    "User-Agent: ZuzuOS/1.0 (Scottish Fold; netd)\r\n"
                    "\r\n";
                tcp_send(slot, (const uint8_t *)req, strlen(req));
            }
        } break;
        case TCP_ESTABLISHED: {
            uint16_t hdr_len = (th->data_offset >> 4) * 4;
            const uint8_t *payload = data + hdr_len;
            uint16_t payload_len = len - hdr_len;

            if (payload_len && their_seq == pcb->rcv_nxt) {   /* in-order data */
                LOG_INFO(LOG_TAG, "RX %u bytes: %.*s", payload_len, payload_len, payload);
                /* (later: hand bytes to the app; for now just log/peek) */
                pcb->rcv_nxt += payload_len;
                tcp_output(pcb, TCP_ACK, NULL, 0);            /* ack what we got */
            }
        } break;
    }
}
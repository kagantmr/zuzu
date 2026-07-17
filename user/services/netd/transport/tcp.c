#include "tcp.h"
#include "tcp_pcb.h"
#include "tcp_out.h"
#include "port.h"
#include "../app/dhcp.h"
#include "../common/netrand.h"
#include "../net/ip.h"
#include <zuzu/log.h>
#include <string.h>

int tcp_connect(ipv4_addr_t remote_ip, port_t remote_port) {
    if (!dhcp_is_bound()) return ERR_NOTCONN;
    int idx = tcp_pcb_alloc();
    if (idx < 0) return ERR_NOMEM;
    tcp_pcb_t *pcb = &tcp_pcbs[idx];
    pcb->remote_ip = remote_ip;
    pcb->remote_port = remote_port;
    pcb->local_ip = netif.ip;
    pcb->local_port = port_alloc();
    if (pcb->local_port == 0) { tcp_pcb_free(idx); return ERR_NOMEM; }
    pcb->snd_nxt = netrand_u32();
    pcb->snd_una = pcb->snd_nxt;
    pcb->rcv_nxt = 0;
    pcb->rcv_rsq = pcb->rcv_nxt;
    pcb->rto_ms = 1000;
    pcb->state = TCP_SYN_SENT;
    int rc = tcp_output(pcb, TCP_SYN, NULL, 0);
    if (rc != ZUZU_OK) { port_release(pcb->local_port); tcp_pcb_free(idx); return rc; }
    LOG_INFO(LOG_TAG, "SYN -> %u.%u.%u.%u:%u", IP4(remote_ip), remote_port);
    return idx;
}

int tcp_listen(int port) {
    int slot = tcp_pcb_alloc();
    if (slot < 0) return ERR_NOMEM;
    tcp_pcb_t *pcb = &tcp_pcbs[slot];
    pcb->active = true;               /* memset cleared it, set it back */
    pcb->local_ip = netif.ip;
    pcb->local_port = port;
    pcb->state = TCP_LISTENING;
    return slot;
}

int tcp_close(int idx) {
    tcp_pcb_t *pcb = &tcp_pcbs[idx];
    int rc = tcp_output(pcb, TCP_FIN | TCP_ACK, NULL, 0);
    if (rc == 0) {
        if (pcb->state == TCP_ESTABLISHED)
            pcb->state = TCP_FIN_WAIT_1;      /* active close */
        else if (pcb->state == TCP_CLOSE_WAIT)
            pcb->state = TCP_LAST_ACK;        /* passive close */
    }
    return rc;
}

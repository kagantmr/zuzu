#include "tcp.h"
#include "../app/dhcp.h"
#include "../common/netrand.h"
#include "../net/ip.h"
#include "port.h"
#include "tcp_out.h"
#include "tcp_pcb.h"
#include <string.h>
#include <util/log.h>

int TcpConnect(Ipv4Addr remote_ip, NetPort remote_port, const TcpCallbacks *cb, void *ctx)
{
    if (!DhcpIsBound())
        return ERR_NOTCONN;
    int idx = TcpPcbAlloc();
    if (idx < 0)
        return ERR_NOMEM;
    TcpPcb *pcb = &tcp_pcbs[idx];
    pcb->remote_ip = remote_ip;
    pcb->remote_port = remote_port;
    pcb->local_ip = netif.ip;
    pcb->local_port = PortAlloc();
    if (pcb->local_port == 0) {
        TcpPcbRelease(pcb, ZUZU_OK);
        return ERR_NOMEM;
    }
    pcb->snd_nxt = NetRandU32();
    pcb->snd_una = pcb->snd_nxt;
    pcb->snd_max = pcb->snd_nxt;
    pcb->rcv_nxt = 0;
    pcb->rcv_rsq = pcb->rcv_nxt;
    pcb->rto_ms = 1000;
    pcb->snd_mss = TCP_MSS;
    pcb->state = TCP_SYN_SENT;
    int rc = TcpOutput(pcb, TCP_SYN, 0);
    RtoStart(pcb);
    if (rc != ZUZU_OK) {
        TcpPcbRelease(pcb, ZUZU_OK);
        return rc;
    }
    RtoStart(pcb);
    pcb->cb = cb;
    pcb->ctx = ctx;
    LOG_INFO(LOG_TAG, "SYN -> %u.%u.%u.%u:%u", IP4(remote_ip), remote_port);
    return idx;
}
int TcpListen(int port, int backlog, const TcpCallbacks *cb, void *ctx)
{
    int slot = TcpPcbAlloc();
    if (slot < 0)
        return ERR_NOMEM;
    TcpPcb *pcb = &tcp_pcbs[slot];
    pcb->active = true; /* memset cleared it, set it back */
    pcb->local_ip = netif.ip;
    pcb->local_port = port;
    pcb->state = TCP_LISTENING;
    pcb->backlog = backlog > 0 ? backlog : TCP_LISTEN_BACKLOG;
    pcb->cb = cb;
    pcb->ctx = ctx;
    return slot;
}

int TcpClose(int idx)
{
    TcpPcb *pcb = &tcp_pcbs[idx];

    switch (pcb->state) {
    case TCP_LISTENING:
        for (int i = 0; i < TCP_MAX_PCB; i++) {
            if (tcp_pcbs[i].active && tcp_pcbs[i].state == TCP_SYN_RCVD &&
                tcp_pcbs[i].parent == idx + 1)
                TcpPcbRelease(&tcp_pcbs[i], ZUZU_OK);
        }
        TcpPcbRelease(pcb, ZUZU_OK);
        return ZUZU_OK;
    case TCP_SYN_SENT:
        TcpPcbRelease(pcb, ZUZU_OK);
        return ZUZU_OK;
    case TCP_ESTABLISHED:
    case TCP_SYN_RCVD:
        pcb->state = TCP_FIN_WAIT_1; /* active close */
        break;
    case TCP_CLOSE_WAIT:
        pcb->state = TCP_LAST_ACK; /* passive close */
        break;
    default:
        return ERR_NOTCONN;
    }

    pcb->fin_pending = true; /* stream ends after last buffered byte */
    TcpXmit(pcb);            /* emits FIN (piggybacked or bare) */
    return ZUZU_OK;
}

void TcpCcInit(TcpPcb *pcb) {
    pcb->cwnd = 10 * pcb->snd_mss; /* IW10, RFC 6928; */
    pcb->ssthresh = UINT32_MAX;
    pcb->recover = pcb->snd_nxt;
}

void *TcpGetCtx(int slot) { return tcp_pcbs[slot].ctx; }

void TcpSetCtx(int slot, void *ctx) { tcp_pcbs[slot].ctx = ctx; }

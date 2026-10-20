#include "tcp_pcb.h"
#include "tcp_out.h"
#include "port.h"
#include <stdlib.h>

TcpPcb tcp_pcbs[TCP_MAX_PCB];

static size_t buf_used;

static bool TcpBufAlloc(uint8_t **buf, size_t size)
{
    if (*buf)
        return true;
    if (buf_used + size > TCP_BUF_BUDGET)
        return false;
    uint8_t *p = malloc(size);
    if (!p)
        return false;
    *buf = p;
    buf_used += size;
    return true;
}

static void TcpBufFree(uint8_t **buf, size_t size)
{
    if (!*buf)
        return;
    free(*buf);
    *buf = NULL;
    buf_used -= size;
}

bool TcpRcvBufAlloc(TcpPcb *pcb) { return TcpBufAlloc(&pcb->rcv_buf, pcb->rcv_size); }

bool TcpSndBufAlloc(TcpPcb *pcb) { return TcpBufAlloc(&pcb->snd_buf, pcb->snd_size); }

int TcpPcbAlloc(void)
{
    for (int i = 0; i < TCP_MAX_PCB; i++) {
        if (!tcp_pcbs[i].active) {
            memset(&tcp_pcbs[i], 0, sizeof(TcpPcb));
            tcp_pcbs[i].active = true;
            tcp_pcbs[i].snd_size = TCP_SND_BUF;
            tcp_pcbs[i].rcv_size = TCP_RCV_BUF;
            tcp_pcbs[i].fin_pending = false;
            return i;
        }
    }
    return ERR_NOMEM;
}

void TcpBufsFree(TcpPcb *pcb)
{
    TcpBufFree(&pcb->snd_buf, pcb->snd_size);
    TcpBufFree(&pcb->rcv_buf, pcb->rcv_size);
}

void TcpPcbFree(int h)
{
    if (h < 0 || h > TCP_MAX_PCB - 1)
        return;
    TimerCancel(&tcp_pcbs[h].rto_timer);
    TimerCancel(&tcp_pcbs[h].tw_timer);
    TimerCancel(&tcp_pcbs[h].persist_timer);
    TimerCancel(&tcp_pcbs[h].fin_wait2_timer);
    TcpBufsFree(&tcp_pcbs[h]);
    tcp_pcbs[h].active = false;
}

int TcpPcbFind(Ipv4Addr local_ip, NetPort local_port, Ipv4Addr remote_ip, NetPort remote_port)
{
    for (int i = 0; i < TCP_MAX_PCB; i++) {
        if (tcp_pcbs[i].local_ip == local_ip && tcp_pcbs[i].local_port == local_port &&
            tcp_pcbs[i].remote_ip == remote_ip && tcp_pcbs[i].remote_port == remote_port &&
            tcp_pcbs[i].active) {
            return i;
        }
    }
    return ERR_NOENT;
}

int TcpPcbFindListener(Ipv4Addr local_ip, NetPort local_port)
{
    for (int i = 0; i < TCP_MAX_PCB; i++) {
        if (tcp_pcbs[i].active && tcp_pcbs[i].state == TCP_LISTENING &&
            tcp_pcbs[i].local_ip == local_ip && tcp_pcbs[i].local_port == local_port) {
            return i;
        }
    }
    return ERR_NOENT;
}

void TcpPcbRelease(TcpPcb *pcb, int err)
{
    const TcpCallbacks *cb = pcb->cb;
    pcb->cb = NULL;
    pcb->state = TCP_CLOSED;
    if (cb && cb->on_closed)
        cb->on_closed(TcpPcbIndex(pcb), err);
    PortRelease(pcb->local_port);
    TcpPcbFree(TcpPcbIndex(pcb));
}

#ifndef TCP_PCB_H
#define TCP_PCB_H

#include "tcp.h"

/* The one and only PCB table. Owned by tcp_pcb.c, shared across the
 * TCP modules (input state machine, output path, connection API). */
extern TcpPcb tcp_pcbs[TCP_MAX_PCB];

/* Grab a free slot and mark it active. Returns index or ERR_NOMEM. */
int TcpPcbAlloc(void);

/* Release a slot back to the pool. Safe to call with an out-of-range index. */
void TcpPcbFree(int h);

/* Tear a connection down: fire on_closed(err) once, cancel the PCB's timers,
 * release its port and return the slot to the pool. err is ZUZU_OK for an
 * orderly end. */
void TcpPcbRelease(TcpPcb *pcb, int err);

#define TCP_NOTIFY(pcb, ev, ...)                                                                   \
    do {                                                                                           \
        if ((pcb)->cb && (pcb)->cb->ev)                                                            \
            (pcb)->cb->ev(__VA_ARGS__);                                                            \
    } while (0)

/* Lazily allocate the receive/send ring. False when the heap or the global
 * TCP_BUF_BUDGET is exhausted. */
/* Return both rings to the heap and the budget; safe to call repeatedly. */
void TcpBufsFree(TcpPcb *pcb);

bool TcpRcvBufAlloc(TcpPcb *pcb);
bool TcpSndBufAlloc(TcpPcb *pcb);

/* Exact 4-tuple match. Returns index or ERR_NOENT. */
int TcpPcbFind(Ipv4Addr local_ip, NetPort local_port, Ipv4Addr remote_ip, NetPort remote_port);

/* Find a listening PCB bound to local_ip:local_port. Returns index or ERR_NOENT. */
int TcpPcbFindListener(Ipv4Addr local_ip, NetPort local_port);

/* Recover a slot index from a PCB pointer (used by timer callbacks). */
static inline int TcpPcbIndex(const TcpPcb *pcb) { return (int)(pcb - tcp_pcbs); }

#endif

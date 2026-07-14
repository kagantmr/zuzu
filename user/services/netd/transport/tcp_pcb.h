#ifndef TCP_PCB_H
#define TCP_PCB_H

#include "tcp.h"

/* The one and only PCB table. Owned by tcp_pcb.c, shared across the
 * TCP modules (input state machine, output path, connection API). */
extern tcp_pcb_t tcp_pcbs[TCP_MAX_PCB];

/* Grab a free slot and mark it active. Returns index or ERR_NOMEM. */
int  tcp_pcb_alloc(void);

/* Release a slot back to the pool. Safe to call with an out-of-range index. */
void tcp_pcb_free(int h);

/* Exact 4-tuple match. Returns index or ERR_NOENT. */
int  tcp_pcb_find(ipv4_addr_t local_ip, port_t local_port,
                  ipv4_addr_t remote_ip, port_t remote_port);

/* Find a listening PCB bound to local_ip:local_port. Returns index or ERR_NOENT. */
int  tcp_pcb_find_listener(ipv4_addr_t local_ip, port_t local_port);

/* Recover a slot index from a PCB pointer (used by timer callbacks). */
static inline int tcp_pcb_index(const tcp_pcb_t *pcb) {
    return (int)(pcb - tcp_pcbs);
}

#endif

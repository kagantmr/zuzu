#ifndef TCP_OUT_H
#define TCP_OUT_H

#include "tcp.h"


/* TCP checksum over the pseudo-header + segment. Returns 0 when a received
 * segment validates; used both to build and to verify segments. */
uint16_t tcp_checksum(ipv4_addr_t src_ip, ipv4_addr_t dst_ip,
                      const uint8_t *seg, uint16_t seg_len);

/* Build and transmit a single segment with the given flags and payload.
 * Advances snd_nxt for SYN/FIN/data. Returns ZUZU_OK or an error code. */
int tcp_output(tcp_pcb_t *pcb, uint8_t flags, const uint8_t *data, uint16_t data_len);

/* Drain the send buffer within the window, segmenting into MSS chunks and
 * arming the retransmit timer if this opens a fresh window. */
int tcp_xmit(tcp_pcb_t *pcb);

/* Retransmission timeout callback. arg is the owning tcp_pcb_t *. */
void tcp_rto_cb(void *arg);

#endif

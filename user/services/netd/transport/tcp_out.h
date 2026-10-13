#ifndef TCP_OUT_H
#define TCP_OUT_H

#include "tcp.h"

/* TCP checksum over the pseudo-header + segment. Returns 0 when a received
 * segment validates; used both to build and to verify segments. */
uint16_t TcpChecksum(Ipv4Addr src_ip, Ipv4Addr dst_ip, const uint8_t *seg, uint16_t seg_len);

/* Build and transmit a single segment with the given flags. Payload bytes are
 * copied from the send ring at snd_nxt straight into the tx frame.
 * Advances snd_nxt for SYN/FIN/data. Returns ZUZU_OK or an error code. */
int TcpOutput(TcpPcb *pcb, uint8_t flags, uint16_t data_len);

/* Drain the send buffer within the window, segmenting into MSS chunks and
 * arming the retransmit timer if this opens a fresh window. */
int TcpXmit(TcpPcb *pcb);

/* Retransmission timeout callback. arg is the owning TcpPcb *. */
void TcpRtoCb(void *arg);

/* Persist (zero-window probe) timer callback. arg is the owning TcpPcb *. */
void TcpPersistCb(void *arg);

void TcpRetransmitHead(TcpPcb *pcb);

void RtoStop(TcpPcb *pcb);
void RtoStart(TcpPcb *pcb);

void TcpSendRst(Ipv4Addr src_ip, Ipv4Addr dst_ip, const TcpSegment *seg);

#endif

#ifndef TCP_H
#define TCP_H

#include "../common/globals.h"
#include <zuzu/types.h>

typedef struct __attribute__((packed)) {
    port_t   src_port;
    port_t   dst_port;
    uint32_t seq;
    uint32_t ack;
    uint8_t  data_offset;
    uint8_t flags;
    uint16_t window;
    uint16_t checksum;
    uint16_t urgent_ptr;
} tcp_hdr_t;

_Static_assert(sizeof(tcp_hdr_t) == 20, "TCP header size");

#define TCP_FIN 0x01
#define TCP_SYN 0x02
#define TCP_RST 0x04
#define TCP_PSH 0x08
#define TCP_ACK 0x10

#define TCP_MAX_PCB 64
#define TCP_DEFAULT_WINDOW 8192

typedef enum {
    TCP_CLOSED = 0,
    TCP_SYN_SENT,
    TCP_ESTABLISHED
} tcp_state_t;

typedef struct {
    ipv4_addr_t local_ip;
    port_t local_port;
    ipv4_addr_t remote_ip;
    port_t remote_port;
    tcp_state_t state;
    uint32_t snd_nxt;
    uint32_t snd_una;
    uint32_t rcv_nxt;
    bool active;
} tcp_pcb_t;

int tcp_connect(ipv4_addr_t remote_ip, port_t remote_port); // , callback later)
void tcp_rx(ipv4_addr_t src_ip, ipv4_addr_t dst_ip, const uint8_t *data, uint16_t len);
int tcp_send(int idx, const uint8_t *data, uint16_t len);

#endif
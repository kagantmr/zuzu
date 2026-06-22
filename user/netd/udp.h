#ifndef UDP_H
#define UDP_H

#include <zuzu/types.h>
#include "globals.h"

typedef struct __attribute__((packed)) {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t length;     // UDP header + data
    uint16_t checksum;
} udp_hdr_t;

static_assert(sizeof(udp_hdr_t) == 8, "udp_hdr_t size");

#define UDP_MAX_PAYLOAD (NIC_MTU - 20 - 8)   // 1472

typedef void (*udp_handler_t)(ipv4_addr_t src_ip, uint16_t src_port,
                              uint16_t dst_port, const uint8_t *data, uint16_t len);
void udp_init(void);
int  udp_bind(uint16_t port, udp_handler_t handler);
void udp_rx(uint8_t *data, uint16_t len, ipv4_addr_t src_ip, ipv4_addr_t dst_ip);
int  udp_tx(ipv4_addr_t dst_ip, uint16_t src_port, uint16_t dst_port,
            const uint8_t *payload, uint16_t payload_len);


#endif

#ifndef ICMP_H
#define ICMP_H

#include <zuzu/types.h>
#include "../common/globals.h"

typedef struct __attribute__((packed)) {
    uint8_t type;
    uint8_t code;
    uint16_t checksum;
    uint16_t identifier;
    uint16_t sequence;
} icmp_hdr_t;

void icmp_rx(uint8_t *payload, size_t payload_len, ipv4_addr_t src_ip);
void icmp_echo_request(ipv4_addr_t dst_ip);

#define ICMP_ECHO_REPLY 0
#define ICMP_ECHO_REQUEST 8

_Static_assert(sizeof(icmp_hdr_t) == 8, "ICMP header is not sized correctly");

#endif /* ICMP_H */
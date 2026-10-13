
#ifndef ICMP_H
#define ICMP_H

#include "../common/globals.h"
#include <types.h>

typedef struct __attribute__((packed)) {
    uint8_t type;
    uint8_t code;
    uint16_t checksum;
    uint16_t identifier;
    uint16_t sequence;
} IcmpHdr;

void IcmpRx(uint8_t *payload, size_t payload_len, Ipv4Addr src_ip);

#define ICMP_ECHO_REPLY 0
#define ICMP_ECHO_REQUEST 8

_Static_assert(sizeof(IcmpHdr) == 8, "ICMP header is not sized correctly");

#endif /* ICMP_H */
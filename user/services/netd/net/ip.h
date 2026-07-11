#ifndef IP_H
#define IP_H

#include <stddef.h>
#include "../common/globals.h"
#include <stdint.h>
#include "../common/txframe.h"

typedef struct {
    uint8_t version_ihl;
    uint8_t dscp_ecn;
    uint16_t total_length;
    uint16_t identification;
    uint16_t flags_fragment_offset;
    uint8_t ttl;
    uint8_t protocol;
    uint16_t header_checksum;
    ipv4_addr_t src_ip;
    ipv4_addr_t dst_ip;
} __attribute__((packed)) ip_header_t;

_Static_assert(sizeof(ip_header_t) == 20, "ip_header_t size wrong");

#define IP_PROTO_ICMP 1
#define IP_PROTO_TCP 6
#define IP_PROTO_UDP 17

#define IP_FLAG_DF (1u << 14)
#define IP_FLAG_MF (1u << 13)

uint16_t inet_checksum(uint8_t *data, size_t len);
/* Incremental checksum helpers for summing non-contiguous regions (pseudo-header
   + L4 segment). Pass accum=0 to the first call, chain the result, then fold. */
uint32_t inet_csum_partial(const uint8_t *data, size_t len, uint32_t accum);
uint16_t inet_csum_fold(uint32_t accum);
void ip_rx(uint8_t *data, uint16_t len, const uint8_t *src_mac);
int ip_tx(uint8_t *payload, uint16_t payload_len, ipv4_addr_t src_ip, ipv4_addr_t dst_ip, uint8_t protocol);
/* Zero-copy send: prepend an IP header onto an already-built L4 frame and hand
   it to ARP/Ethernet. The builder must have reserved headroom for the Ethernet
   and IP headers (plus whatever L4 header it already prepended). */
int ip_send(txframe_t *f, ipv4_addr_t src_ip, ipv4_addr_t dst_ip, uint8_t protocol);

#endif /* IP_H */
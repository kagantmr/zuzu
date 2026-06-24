#include "ip.h"
#include "eth.h"
#include "icmp.h"
#include "udp.h"
#include "arp.h"
#include <stdio.h>
#include <convert.h>
#include <malloc.h>
#include <mem.h>

static int id_counter = 0;

/* Accumulate big-endian 16-bit words of "data" into "accum" without folding, so
   that several non-contiguous regions (e.g. a pseudo-header + segment) can be
   summed together. Each region passed in must have an even length except the
   last, otherwise the trailing-byte padding misaligns the following region. */
uint32_t inet_csum_partial(const uint8_t *data, size_t len, uint32_t accum) {
    size_t i = 0;
    for (; i + 1 < len; i += 2) {
        accum += (data[i] << 8) | data[i + 1];
    }
    if (len & 1) {
        accum += (uint32_t)data[len - 1] << 8;
    }
    return accum;
}

/* Fold a partial sum down to 16 bits and take the one's complement. */
uint16_t inet_csum_fold(uint32_t accum) {
    while (accum >> 16) {
        accum = (accum & 0xFFFF) + (accum >> 16);
    }
    return (uint16_t)~accum;
}

uint16_t inet_checksum(uint8_t *data, size_t len) {
    return inet_csum_fold(inet_csum_partial(data, len, 0));
}

void ip_rx(uint8_t *data, uint16_t len, const uint8_t *src_mac) {
    if (len < 20) {
        return;
    }
    ip_header_t *hdr = (ip_header_t *)data;

    if ((hdr->version_ihl & (0xF0)) != 0x40) {
        return;     // we accept ipv4 only (for now)
    }
    uint8_t hdr_len = (hdr->version_ihl & (0x0F)) * 4;
    if (len < hdr_len || ntohs(hdr->total_length) < hdr_len
         || ntohs(hdr->total_length) > len) {
        return; // anomalies with length?
    }

    uint16_t cs = inet_checksum(data, hdr_len);
    if (cs || (hdr->dst_ip != netif.ip && hdr->dst_ip != BROADCAST_IP)) {
        return;
    }

    /* Header is valid and addressed to us so it's safe to cache the L2/L3
       mapping of the sender. */
    arp_learn(hdr->src_ip, src_mac);

    if (ntohs(hdr->flags_fragment_offset) & IP_FLAG_MF ||
    ntohs(hdr->flags_fragment_offset) & 0x1FFF) {
        return; // no reassembly
    }

    uint8_t *payload = data + hdr_len;
    size_t payload_len = ntohs(hdr->total_length) - hdr_len;
    switch (hdr->protocol)
    {
    case IP_PROTO_ICMP:
        icmp_rx(payload, payload_len, hdr->src_ip);
        break;
    case IP_PROTO_UDP:
        udp_rx(payload, payload_len, hdr->src_ip, hdr->dst_ip);
        break;
    default:
        return;
    }

}

int ip_send(txframe_t *f, ipv4_addr_t src_ip, ipv4_addr_t dst_ip, uint8_t protocol) {
    /* The L4 header + payload are already in the builder; prepend the IP
       header in front of them without copying anything. */
    uint16_t payload_len = txframe_len(f);
    if ((size_t)sizeof(ip_header_t) + payload_len > NIC_MTU)
        return ERR_OVERFLOW; /* TODO: fragment here when DF is clear */

    ip_header_t *hdr = (ip_header_t *)txframe_prepend(f, sizeof(ip_header_t));
    if (!hdr)
        return ERR_OVERFLOW;

    hdr->version_ihl = 0x45;
    hdr->dscp_ecn = 0;
    hdr->total_length = htons(sizeof(ip_header_t) + payload_len);
    hdr->identification = htons(id_counter++);
    hdr->flags_fragment_offset = htons(IP_FLAG_DF);
    hdr->ttl = 64;
    hdr->protocol = protocol;
    hdr->header_checksum = 0;
    hdr->src_ip = src_ip;
    hdr->dst_ip = dst_ip;
    hdr->header_checksum = htons(inet_checksum((uint8_t *)hdr, sizeof(ip_header_t)));

    /* Next-hop selection: an on-link destination is resolved (ARPed) directly;
       anything outside our subnet goes via the default gateway. The L3 dst in
       the header stays the real destination -- only the L2 target changes.
       Limited broadcast stays direct. Degenerate one-route case of what becomes
       a route lookup when multiple interfaces/routes exist. */
    ipv4_addr_t next_hop = dst_ip;
    if (dst_ip != BROADCAST_IP && ((dst_ip ^ netif.ip) & netif.netmask))
        next_hop = netif.gateway;

    arp_send_frame(next_hop, ETH_TYPE_IP, f);
    return ZUZU_OK;
}

int ip_tx(uint8_t *payload, uint16_t payload_len, ipv4_addr_t src_ip, ipv4_addr_t dst_ip, uint8_t protocol) {
    if ((size_t)sizeof(ip_header_t) + payload_len > NIC_MTU)
        return ERR_OVERFLOW; /* TODO: fragment here when DF is clear */

    /* Reserve room for the Ethernet + IP headers in front of the payload, then
       copy the caller's payload once into its final position in the slot. */
    txframe_t f;
    int rc = txframe_init(&f, sizeof(eth_hdr_t) + sizeof(ip_header_t));
    if (rc != ZUZU_OK)
        return rc;

    void *dst = txframe_append(&f, payload_len);
    if (!dst)
        return ERR_OVERFLOW;
    memcpy(dst, payload, payload_len);

    return ip_send(&f, src_ip, dst_ip, protocol);
}

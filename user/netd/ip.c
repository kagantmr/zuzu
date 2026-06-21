#include "ip.h"
#include "eth.h"
#include "icmp.h"
#include "arp.h"
#include <stdio.h>
#include <convert.h>
#include <malloc.h>
#include <mem.h>

static int id_counter = 0;

uint16_t inet_checksum(uint8_t *data, size_t len) {
    if (len == 0) return 0;
    uint32_t accum = 0;
    for (size_t i = 0; i < len - 1; i += 2) {
        accum += (data[i] << 8) | data[i + 1];
    }
    if (len % 2) { 
        accum += (data[len - 1] << 8);
    }

    while (accum >> 16) {
        accum = (accum & 0xFFFF) + (accum >> 16);
    }
    return (uint16_t)~accum;
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
    if (cs || (hdr->dst_ip != ZUZU_IP && hdr->dst_ip != BROADCAST_IP)) {
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

    default:
        return;
    }

}

void ip_tx(uint8_t *payload, uint16_t payload_len, ipv4_addr_t src_ip, ipv4_addr_t dst_ip, uint8_t protocol) {
    ip_header_t hdr;
    hdr.version_ihl = 0x45;
    hdr.dscp_ecn = 0;
    hdr.total_length = htons(20 + payload_len);
    hdr.identification = htons(id_counter++);
    hdr.protocol = protocol;
    hdr.ttl = 64;
    hdr.header_checksum = 0;
    hdr.src_ip = src_ip;
    hdr.dst_ip = dst_ip;
    hdr.flags_fragment_offset = htons(IP_FLAG_DF);
    
    hdr.header_checksum = htons(inet_checksum((uint8_t *)&hdr, 20));

    uint8_t *frame = (uint8_t *)malloc(sizeof(hdr)+ payload_len);
    if (!frame)
        return;
    memcpy(frame, &hdr, 20);
    memcpy(frame + 20, payload, payload_len);

    arp_send_or_queue(dst_ip, ETH_TYPE_IP, frame, sizeof(hdr)+ payload_len);

    free(frame);
}

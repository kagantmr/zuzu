#include "ip.h"
#include "eth.h"
#include "arp.h"
#include <stdio.h>
#include <convert.h>
#include <malloc.h>
#include <mem.h>

static int id_counter = 0;

#define ICMP_ECHO_REPLY 0
#define ICMP_ECHO_REQUEST 8

typedef struct {
    uint8_t type;
    uint8_t code;
    uint16_t checksum;
    uint16_t identifier;
    uint16_t sequence;
} __attribute__((packed)) icmp_echo_header_t;

_Static_assert(sizeof(icmp_echo_header_t) == 8, "icmp_echo_header_t size wrong");

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

void ip_rx(uint8_t *data, uint16_t len) {
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

    if (ntohs(hdr->flags_fragment_offset) & IP_FLAG_MF ||
    ntohs(hdr->flags_fragment_offset) & 0x1FFF) {
        return; // no reassembly
    }

    uint8_t *payload = data + hdr_len;
    size_t payload_len = ntohs(hdr->total_length) - hdr_len;
    printf("IP packet in.\n protocol: %d, total_length: %d, ttl: %d\n", hdr->protocol, ntohs(hdr->total_length), hdr->ttl);
    printf("src: %u.%u.%u.%u\n", data[12], data[13], data[14], data[15]);
    printf("dst: %u.%u.%u.%u\n", data[16], data[17], data[18], data[19]);
    switch (hdr->protocol)
    {
    case IP_PROTO_ICMP:
        if (payload_len < sizeof(icmp_echo_header_t)) {
            return;
        }
        if (inet_checksum(payload, payload_len) != 0) {
            return;
        }

        icmp_echo_header_t *icmp = (icmp_echo_header_t *)payload;
        if (icmp->type != ICMP_ECHO_REQUEST || icmp->code != 0) {
            return;
        }

        uint8_t *reply = malloc(payload_len);
        if (!reply) {
            return;
        }
        memcpy(reply, payload, payload_len);

        icmp_echo_header_t *reply_hdr = (icmp_echo_header_t *)reply;
        reply_hdr->type = ICMP_ECHO_REPLY;
        reply_hdr->checksum = 0;
        reply_hdr->checksum = inet_checksum(reply, payload_len);

        ip_tx(reply, payload_len, ZUZU_IP, hdr->src_ip, IP_PROTO_ICMP);
        free(reply);
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
    
    hdr.header_checksum = inet_checksum((uint8_t *)&hdr, 20);

    mac_addr_t dst_mac;
    if (arp_lookup(dst_ip, dst_mac)) {
        return;
    }

    uint8_t *frame = (uint8_t *)malloc(sizeof(hdr)+ payload_len);
    memcpy(frame, &hdr, 20);
    memcpy(frame + 20, payload, payload_len);

    eth_tx(dst_mac, ETH_TYPE_IP, frame, sizeof(hdr)+ payload_len);

    free(frame);
}

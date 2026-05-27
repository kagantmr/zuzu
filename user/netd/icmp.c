#include "icmp.h"
#include <malloc.h>
#include <convert.h>
#include "ip.h"
#include <stdio.h>


void icmp_rx(uint8_t *payload, size_t payload_len, ipv4_addr_t src_ip) {
    if (payload_len < 8 || !payload) {
        return;
    }
    icmp_hdr_t *hdr = (icmp_hdr_t *)payload;
    switch (hdr->type) {
        case ICMP_ECHO_REPLY: {
            break;
        }
        case ICMP_ECHO_REQUEST: {
            if (inet_checksum(payload, payload_len) != 0) {
                return;
            }

            uint8_t *reply = malloc(payload_len);
            if (!reply) {
                return;
            }
            memcpy(reply, payload, payload_len);

            icmp_hdr_t *reply_hdr = (icmp_hdr_t *)reply;
            reply_hdr->type = ICMP_ECHO_REPLY;
            reply_hdr->checksum = 0;
            reply_hdr->checksum = htons(inet_checksum(reply, payload_len));

            //printf("ICMP Echo Request\n");
            //printf("src: %u.%u.%u.%u\n", src_ip & 0xFF, (src_ip >> 8) & 0xFF, (src_ip >> 16) & 0xFF, (src_ip >> 24) & 0xFF);

            ip_tx(reply, payload_len, ZUZU_IP, src_ip, IP_PROTO_ICMP);
            free(reply);
            break;
        }

        default: {
            break;
        }
    }
}
#include "icmp.h"
#include <malloc.h>
#include <convert.h>
#include "ip.h"
#include <stdio.h>
#include <zuzu/log.h>

#define LOG_TAG "netd"

/* expand a network-order ipv4_addr_t to four %u args (a.b.c.d) */
#define IP4(x) (unsigned)((x) & 0xff), (unsigned)(((x) >> 8) & 0xff), \
               (unsigned)(((x) >> 16) & 0xff), (unsigned)(((x) >> 24) & 0xff)

void icmp_rx(uint8_t *payload, size_t payload_len, ipv4_addr_t src_ip) {
    if (payload_len < 8 || !payload) {
        return;
    }
    icmp_hdr_t *hdr = (icmp_hdr_t *)payload;
    switch (hdr->type) {
        case ICMP_ECHO_REPLY: {
            LOG_INFO(LOG_TAG, "ICMP echo reply from %u.%u.%u.%u", IP4(src_ip));
            break;
        }
        case ICMP_ECHO_REQUEST: {
            if (inet_checksum(payload, payload_len) != 0) {
                return;
            }

            static rate_limiter_t echo_reply_rl;
            if (!rate_allow(&echo_reply_rl, 16, 32)) {
                return; /* flood guard: drop excess echo requests */
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

            if (ip_tx(reply, payload_len, netif.ip, src_ip, IP_PROTO_ICMP) != ZUZU_OK)
                LOG_WARN(LOG_TAG, "echo reply to %u.%u.%u.%u dropped (oversize?)", IP4(src_ip));
            free(reply);
            break;
        }

        default: {
            break;
        }
    }
}

/* Build and send an ICMP echo request (ping out). Used to exercise the ARP
   resolve/queue path against a host whose MAC netd hasn't learned yet. */
void icmp_echo_request(ipv4_addr_t dst_ip) {
    uint8_t pkt[sizeof(icmp_hdr_t) + 32];
    icmp_hdr_t *hdr = (icmp_hdr_t *)pkt;
    hdr->type = ICMP_ECHO_REQUEST;
    hdr->code = 0;
    hdr->checksum = 0;
    hdr->identifier = htons(1);
    hdr->sequence = htons(1);
    for (size_t i = 0; i < 32; i++)
        pkt[sizeof(icmp_hdr_t) + i] = (uint8_t)i;
    hdr->checksum = htons(inet_checksum(pkt, sizeof(pkt)));
    ip_tx(pkt, sizeof(pkt), netif.ip, dst_ip, IP_PROTO_ICMP);
}

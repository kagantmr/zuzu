#include "icmp.h"
#include "ip.h"
#include <convert.h>
#include <malloc.h>
#include <stdio.h>
#include <util/log.h>

#define LOG_TAG "netd"

/* expand a network-order Ipv4Addr to four %u args (a.b.c.d) */
#define IP4(x)                                                                                     \
    (unsigned)((x) & 0xff), (unsigned)(((x) >> 8) & 0xff), (unsigned)(((x) >> 16) & 0xff),         \
        (unsigned)(((x) >> 24) & 0xff)

void IcmpRx(uint8_t *payload, size_t payload_len, Ipv4Addr src_ip)
{
    if (payload_len < 8 || !payload) {
        return;
    }
    IcmpHdr *hdr = (IcmpHdr *)payload;
    switch (hdr->type) {
    case ICMP_ECHO_REPLY: {
        LOG_INFO(LOG_TAG, "ICMP echo reply from %u.%u.%u.%u", IP4(src_ip));
        break;
    }
    case ICMP_ECHO_REQUEST: {
        if (InetChecksum(payload, payload_len) != 0) {
            return;
        }

        static RateLimiter echo_reply_rl;
        if (!RateAllow(&echo_reply_rl, 16, 32)) {
            return; /* flood guard: drop excess echo requests */
        }

        uint8_t *reply = malloc(payload_len);
        if (!reply) {
            return;
        }
        memcpy(reply, payload, payload_len);

        IcmpHdr *reply_hdr = (IcmpHdr *)reply;
        reply_hdr->type = ICMP_ECHO_REPLY;
        reply_hdr->checksum = 0;
        reply_hdr->checksum = htons(InetChecksum(reply, payload_len));

        if (IpTx(reply, payload_len, netif.ip, src_ip, IP_PROTO_ICMP) != ZUZU_OK)
            LOG_WARN(LOG_TAG, "echo reply to %u.%u.%u.%u dropped (oversize?)", IP4(src_ip));
        free(reply);
        break;
    }

    default: {
        break;
    }
    }
}

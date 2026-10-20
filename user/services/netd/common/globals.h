#ifndef NETD_GLOBALS_H
#define NETD_GLOBALS_H

#include <net/packetring.h>
#include <net/protocols/nic.h>
#include <stdbool.h>
#include <types.h>
#include <zuzu/err.h>
#include <zuzu/syspage.h>
#include <zuzu/uptime.h>

#define BROADCAST_MAC {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}
#define BROADCAST_IP 0xFFFFFFFF

#define LOG_TAG "netd"

/* expand a network-order Ipv4Addr to four %u args (a.b.c.d) */
#define IP4(x)                                                                                     \
    (unsigned)((x) & 0xff), (unsigned)(((x) >> 8) & 0xff), (unsigned)(((x) >> 16) & 0xff),         \
        (unsigned)(((x) >> 24) & 0xff)

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

typedef uint32_t Ipv4Addr;
typedef uint16_t NetPort;
typedef uint8_t MacAddr[6];

/* One network interface's L2/L3 config. */
typedef struct {
    Ipv4Addr ip;
    Ipv4Addr netmask;
    Ipv4Addr gateway;
    Ipv4Addr dns;
    MacAddr mac;
} Netif;

static inline uint32_t NetNowMs(void) { return (uint32_t)UptimeMs(); }

typedef struct {
    uint32_t tokens;
    uint32_t last_ms;
} RateLimiter;

static inline bool RateAllow(RateLimiter *rl, uint32_t rate, uint32_t burst)
{
    uint32_t now = NetNowMs();
    if (rl->last_ms == 0) { /* first use: start full */
        rl->tokens = burst;
        rl->last_ms = now ? now : 1;
    } else {
        uint32_t refill = (uint32_t)(((uint64_t)(now - rl->last_ms) * rate) / 1000u);
        if (refill) {
            uint32_t t = rl->tokens + refill;
            rl->tokens = t > burst ? burst : t;
            rl->last_ms = now;
        }
    }
    if (rl->tokens == 0)
        return false;
    rl->tokens--;
    return true;
}

extern NicRing *tx_ring, *rx_ring;
extern NicStatsBlock *stats;
extern Handle g_svc_port;
extern Handle g_event;
extern Handle g_nic_doorbell_ev; /* shared with the NIC driver: RX bit in, TX bit out */
extern Netif netif;

#endif

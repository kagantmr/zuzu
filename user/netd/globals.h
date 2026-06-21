#ifndef NETD_GLOBALS_H
#define NETD_GLOBALS_H

#include <zuzu/packetring.h>
#include <zuzu/types.h>
#include <zuzu/syspage.h>
#include <stdbool.h>

#define ZUZU_IP ((ipv4_addr_t)htonl((192u << 24) | (168u << 16) | (1u << 8) | 15u)) // currently static, change later with DHCP
#define BROADCAST_MAC {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}
#define BROADCAST_IP 0xFFFFFFFF

typedef uint32_t ipv4_addr_t;
typedef uint8_t mac_addr_t[6];

/* Monotonic millisecond clock from the read-only syspage (no syscall). */
static inline uint32_t net_now_ms(void) {
    syspage_t *sp = (syspage_t *)SYSPAGE;
    uint32_t hz = sp->tick_hz ? sp->tick_hz : 1000u;
    return (uint32_t)((sp->uptime_ticks * 1000ull) / hz);
}

/* Token-bucket rate limiter: allows up to `burst` events, refilled at `rate`
   per second. Zero-initialized limiters self-fill to `burst` on first use. */
typedef struct { uint32_t tokens; uint32_t last_ms; } rate_limiter_t;

static inline bool rate_allow(rate_limiter_t *rl, uint32_t rate, uint32_t burst) {
    uint32_t now = net_now_ms();
    if (rl->last_ms == 0) {                 /* first use: start full */
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

extern nic_ring_t *tx_ring, *rx_ring;
extern handle_t nic_port;
extern handle_t nic_ntfn;
extern handle_t handles[2];
extern mac_addr_t mac;

#define drv_port nic_port
#define netd_port handles[0]


#endif

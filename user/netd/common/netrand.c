#include "netrand.h"

// xorshift32 for TCP
static uint32_t rng_state;

void netrand_init(void) {
    rng_state = net_now_ms();
    rng_state ^= ((uint32_t)netif.mac[2] << 24) | ((uint32_t)netif.mac[3] << 16)
               | ((uint32_t)netif.mac[4] << 8)  |  (uint32_t)netif.mac[5];
    if (!rng_state) rng_state = 0xA5A5A5A5;   /* xorshift must never be 0 */
}

uint32_t netrand_u32(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}
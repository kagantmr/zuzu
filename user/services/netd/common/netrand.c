#include "netrand.h"
#include <arch/cycles.h>

// xorshift32 for TCP
static uint32_t rng_state;

void NetRandInit(void)
{
    rng_state = NetNowMs() ^ ArchMeasure();
    rng_state ^= ((uint32_t)netif.mac[2] << 24) | ((uint32_t)netif.mac[3] << 16) |
                 ((uint32_t)netif.mac[4] << 8) | (uint32_t)netif.mac[5];
    if (!rng_state)
        rng_state = 0xA5A5A5A5; /* xorshift must never be 0 */
}

uint32_t NetRandU32(void)
{
    rng_state ^= ArchMeasure();
    if (!rng_state)
        rng_state = 0xA5A5A5A5;
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}
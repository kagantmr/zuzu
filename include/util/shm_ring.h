#ifndef ZUZU_SHM_RING_H
#define ZUZU_SHM_RING_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>


typedef struct {
    uint32_t size;          /* power of 2 */
    volatile uint32_t head; /* producer-owned */
    volatile uint32_t tail; /* consumer-owned */
} ShmRingHdr;

static inline void ShmRingInit(ShmRingHdr *r, uint32_t size) {
    r->size = size;
    r->head = 0;
    r->tail = 0;
}

static inline uint32_t ShmRingAvail(const ShmRingHdr *r) { return r->head - r->tail; }
static inline uint32_t ShmRingFree(const ShmRingHdr *r) { return r->size - ShmRingAvail(r); }

static inline uint32_t ShmRingPush(ShmRingHdr *r, uint8_t *data, const uint8_t *src, uint32_t len) {
    uint32_t cap = ShmRingFree(r);
    if (len > cap)
        len = cap;
    for (uint32_t i = 0; i < len; i++)
        data[(r->head + i) & (r->size - 1)] = src[i];
    r->head += len; /* publish after the writes */
    return len;
}

static inline uint32_t ShmRingPop(ShmRingHdr *r, const uint8_t *data, uint8_t *dst, uint32_t len) {
    uint32_t cap = ShmRingAvail(r);
    if (len > cap)
        len = cap;
    for (uint32_t i = 0; i < len; i++)
        dst[i] = data[(r->tail + i) & (r->size - 1)];
    r->tail += len;
    return len;
}

#ifdef __cplusplus
}
#endif
#endif
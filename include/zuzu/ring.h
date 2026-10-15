#ifndef ZUZU_RING_H
#define ZUZU_RING_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t *buf;
    size_t size;
    uint32_t head;
    uint32_t tail;
} RingBuffer;

void RingInit(RingBuffer *r, uint8_t *buf, uint32_t size);

int RingPush(RingBuffer *r, uint8_t byte);

int RingPop(RingBuffer *r, uint8_t *out);

int RingPeek(const RingBuffer *r, uint8_t *out);

uint32_t RingAvail(const RingBuffer *r);

int RingFull(const RingBuffer *r);

uint32_t RingPushBuffer(RingBuffer *r, const uint8_t *src, uint32_t len);

uint32_t RingPopIntoBuffer(RingBuffer *r, uint8_t *dst, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif // ZUZU_RING_H

#ifndef ZUZU_RING_H
#define ZUZU_RING_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint8_t  *buf;
    size_t  size;   // must be power of 2
    uint32_t  head;   // written by producer
    uint32_t  tail;   // written by consumer
} ring_t;

void     ring_init(ring_t *r, uint8_t *buf, uint32_t size);
int      ring_push(ring_t *r, uint8_t byte);       // returns 0 or -1 if full
int      ring_pop(ring_t *r, uint8_t *out);        // returns 0 or -1 if empty
int      ring_peek(const ring_t *r, uint8_t *out);
uint32_t ring_avail(const ring_t *r);              // bytes available to read
int      ring_full(const ring_t *r);

// bulk variants for UART DMA use
uint32_t ring_push_buf(ring_t *r, const uint8_t *src, uint32_t len);
uint32_t ring_pop_buf(ring_t *r, uint8_t *dst, uint32_t len);

#endif // ZUZU_RING_H
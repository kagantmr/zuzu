#include <util/ring.h>
#include <string.h>

void RingInit(RingBuffer *r, uint8_t *buf, uint32_t size) {
    r->buf = buf;
    r->size = size;
    r->head = 0;
    r->tail = 0;
}

int RingPush(RingBuffer *r, uint8_t byte) {
    uint32_t next = (r->head + 1) & (r->size - 1);
    if (next == r->tail) return -1; // full
    r->buf[r->head & (r->size - 1)] = byte;
    r->head = next;
    return 0;
}

int RingPop(RingBuffer *r, uint8_t *out) {
    if (r->tail == r->head) return -1; // empty
    *out = r->buf[r->tail & (r->size - 1)];
    r->tail = (r->tail + 1) & (r->size - 1);
    return 0;
}

int RingPeek(const RingBuffer *r, uint8_t *out) {
    if (r->tail == r->head) return -1;
    *out = r->buf[r->tail & (r->size - 1)];
    return 0;
}

uint32_t RingAvail(const RingBuffer *r) {
    return (r->head - r->tail) & (r->size - 1);
}

int RingFull(const RingBuffer *r) {
    return RingAvail(r) == r->size - 1;
}

uint32_t RingPushBuffer(RingBuffer *r, const uint8_t *src, uint32_t len) {
    uint32_t written = 0;
    while (written < len) {
        if (RingPush(r, src[written]) != 0) break;
        written++;
    }
    return written;
}

uint32_t RingPopIntoBuffer(RingBuffer *r, uint8_t *dst, uint32_t len) {
    uint32_t read = 0;
    while (read < len) {
        if (RingPop(r, &dst[read]) != 0) break;
        read++;
    }
    return read;
}

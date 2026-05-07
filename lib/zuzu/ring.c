#include <zuzu/ring.h>
#include <string.h>

void ring_init(ring_t *r, uint8_t *buf, uint32_t size) {
    r->buf = buf;
    r->size = size;
    r->head = 0;
    r->tail = 0;
}

int ring_push(ring_t *r, uint8_t byte) {
    uint32_t next = (r->head + 1) & (r->size - 1);
    if (next == r->tail) return -1; // full
    r->buf[r->head & (r->size - 1)] = byte;
    r->head = next;
    return 0;
}

int ring_pop(ring_t *r, uint8_t *out) {
    if (r->tail == r->head) return -1; // empty
    *out = r->buf[r->tail & (r->size - 1)];
    r->tail = (r->tail + 1) & (r->size - 1);
    return 0;
}

int ring_peek(const ring_t *r, uint8_t *out) {
    if (r->tail == r->head) return -1;
    *out = r->buf[r->tail & (r->size - 1)];
    return 0;
}

uint32_t ring_avail(const ring_t *r) {
    return (r->head - r->tail) & (r->size - 1);
}

int ring_full(const ring_t *r) {
    return ring_avail(r) == r->size - 1;
}

uint32_t ring_push_buf(ring_t *r, const uint8_t *src, uint32_t len) {
    uint32_t written = 0;
    while (written < len) {
        if (ring_push(r, src[written]) != 0) break;
        written++;
    }
    return written;
}

uint32_t ring_pop_buf(ring_t *r, uint8_t *dst, uint32_t len) {
    uint32_t read = 0;
    while (read < len) {
        if (ring_pop(r, &dst[read]) != 0) break;
        read++;
    }
    return read;
}

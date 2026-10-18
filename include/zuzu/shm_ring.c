/**
 * @file shm_ring.c
 * @brief Implementation of the shared-memory ring buffers.
 */

#include <arch/barrier.h>
#include <string.h>
#include <zuzu/shm_ring.h>

void ShmRingInit(ShmRingHdr *r, uint32_t size)
{
    r->size = size;
    r->head = 0;
    r->tail = 0;
}

uint32_t ShmRingLoadHead(const ShmRingHdr *r)
{
    uint32_t head = r->head;
    ArchDmb();
    return head;
}

uint32_t ShmRingLoadTail(const ShmRingHdr *r)
{
    uint32_t tail = r->tail;
    ArchDmb();
    return tail;
}

void ShmRingPublishHead(ShmRingHdr *r, uint32_t head)
{
    ArchDmb();
    r->head = head;
}

void ShmRingPublishTail(ShmRingHdr *r, uint32_t tail)
{
    ArchDmb();
    r->tail = tail;
}

uint32_t ShmRingAvail(const ShmRingHdr *r)
{
    uint32_t avail = ShmRingLoadHead(r) - r->tail;
    return avail > r->size ? r->size : avail;
}

uint32_t ShmRingFree(const ShmRingHdr *r)
{
    uint32_t avail = r->head - ShmRingLoadTail(r);
    return avail > r->size ? 0 : r->size - avail;
}

void ShmRingWriteAt(uint8_t *data, uint32_t size, uint32_t pos, const void *src, uint32_t n)
{
    uint32_t off = pos & (size - 1);
    uint32_t first = n < size - off ? n : size - off;
    memcpy(data + off, src, first);
    memcpy(data, (const uint8_t *)src + first, n - first);
}

void ShmRingReadAt(const uint8_t *data, uint32_t size, uint32_t pos, void *dst, uint32_t n)
{
    uint32_t off = pos & (size - 1);
    uint32_t first = n < size - off ? n : size - off;
    memcpy(dst, data + off, first);
    memcpy((uint8_t *)dst + first, data, n - first);
}

uint32_t ShmRingPush(ShmRingHdr *r, uint8_t *data, const void *src, uint32_t len)
{
    uint32_t cap = ShmRingFree(r);
    if (len > cap)
        len = cap;
    ShmRingWriteAt(data, r->size, r->head, src, len);
    ShmRingPublishHead(r, r->head + len);
    return len;
}

uint32_t ShmRingPop(ShmRingHdr *r, const uint8_t *data, void *dst, uint32_t len)
{
    uint32_t cap = ShmRingAvail(r);
    if (len > cap)
        len = cap;
    ShmRingReadAt(data, r->size, r->tail, dst, len);
    ShmRingPublishTail(r, r->tail + len);
    return len;
}

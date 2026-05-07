#include <zuzu/arena.h>
#include <string.h>
#include <mem.h>

void arena_init(arena_t *a, void *buf, size_t size) {
    a->base = (uint8_t *)buf;
    a->size = size;
    a->offset = 0;
}

void *arena_alloc(arena_t *a, size_t size) {
    if (a->offset + size > a->size)
        return NULL;
    void *p = a->base + a->offset;
    a->offset += size;
    return p;
}

void *arena_alloc_aligned(arena_t *a, size_t size, size_t align) {
    uintptr_t base_addr = (uintptr_t)(a->base + a->offset);
    uintptr_t aligned = (base_addr + (align - 1)) & ~(align - 1);
    size_t pad = aligned - base_addr;
    if (a->offset + pad + size > a->size)
        return NULL;
    a->offset += pad;
    return arena_alloc(a, size);
}

void arena_reset(arena_t *a) {
    a->offset = 0;
}

void arena_destroy(arena_t *a) {
    memset(a, 0, sizeof(*a));
}

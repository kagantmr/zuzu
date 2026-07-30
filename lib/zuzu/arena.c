#include <zuzu/arena.h>
#include <string.h>

void ArenaInit(Arena *a, void *buf, size_t size) {
    a->base = (uint8_t *)buf;
    a->size = size;
    a->offset = 0;
}

void *ArenaAlloc(Arena *a, size_t size) {
    if (a->offset + size > a->size)
        return NULL;
    void *p = a->base + a->offset;
    a->offset += size;
    return p;
}

void *ArenaAllocAligned(Arena *a, size_t size, size_t align) {
    uintptr_t base_addr = (uintptr_t)(a->base + a->offset);
    uintptr_t aligned = (base_addr + (align - 1)) & ~(align - 1);
    size_t pad = aligned - base_addr;
    if (a->offset + pad + size > a->size)
        return NULL;
    a->offset += pad;
    return ArenaAlloc(a, size);
}

void ArenaReset(Arena *a) {
    a->offset = 0;
}

void ArenaDestroy(Arena *a) {
    memset(a, 0, sizeof(*a));
}

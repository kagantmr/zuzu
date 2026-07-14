#ifndef ZUZU_ARENA_H
#define ZUZU_ARENA_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint8_t *base;
    size_t   size;
    size_t   offset;
} arena_t;

void  arena_init(arena_t *a, void *buf, size_t size);
void *arena_alloc(arena_t *a, size_t size);
void *arena_alloc_aligned(arena_t *a, size_t size, size_t align);
void  arena_reset(arena_t *a);    // free everything, keep buffer
void  arena_destroy(arena_t *a);  // zero the struct

#ifdef __cplusplus
}
#endif

#endif // ZUZU_ARENA_H
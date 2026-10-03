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
} Arena;

void  ArenaInit(Arena *a, void *buf, size_t size);
void *ArenaAlloc(Arena *a, size_t size);
void *ArenaAllocAligned(Arena *a, size_t size, size_t align);
void  ArenaReset(Arena *a);    // free everything, keep buffer
void  ArenaDestroy(Arena *a);  // zero the struct

#ifdef __cplusplus
}
#endif

#endif // ZUZU_ARENA_H
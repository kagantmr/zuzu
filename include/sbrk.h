#ifndef SBRK_H
#define SBRK_H

#include <zuzu/types.h>

typedef struct arena {
    vaddr_t base;    // heap_base
    vaddr_t brk;     // heap_brk
    vaddr_t mapped;  // heap_end
} arena_t;

void *sbrk(intptr_t incr);

#endif
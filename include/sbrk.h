#ifndef SBRK_H
#define SBRK_H

#include <zuzu/types.h>

typedef struct arena {
    VirtAddr base;    // heap_base
    VirtAddr brk;     // heap_brk
    VirtAddr mapped;  // heap_end
} Arena;

/**
 * @brief Adjusts the program's data segment size by the specified increment.
 * 
 * @param incr The number of bytes to increase (or decrease if negative) the data segment size.
 * @return void* Returns the previous end of the data segment (the old program break), or (void *)-1 on error.
 */
void *sbrk(intptr_t incr);

#endif
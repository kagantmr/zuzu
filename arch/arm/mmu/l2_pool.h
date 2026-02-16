#ifndef L2_POOL_H
#define L2_POOL_H

#include "stddef.h"
#include "stdint.h"

#define MAX_L2_PAGES 32

typedef struct {
    uintptr_t page_pa;    // PA of the 4KB page from PMM
    uint8_t   used_mask;  // bits 0-3: which 1KB slots are occupied
} l2_pool_entry_t;



uintptr_t l2_pool_alloc(void);
void l2_pool_free(uintptr_t l2_pa);

#endif
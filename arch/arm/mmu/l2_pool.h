#ifndef ARCH_ARM_L2_POOL_H
#define ARCH_ARM_L2_POOL_H

#include <stddef.h>
#include <stdint.h>
#include <types.h>

typedef struct L2PtPoolEntry {
    uintptr_t page_pa;
    uint8_t used_mask;
    struct L2PtPoolEntry *next;
} L2PtPoolEntry;

uintptr_t L2PtPoolAlloc(void);

void L2PtPoolFree(PhysAddr l2_pa);

#endif

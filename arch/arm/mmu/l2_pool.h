/**
 * l2_pool.h - Header for L2 page pool allocator in ARM MMU
 * To optimize memory usage for page tables, we allocate L2 page tables in chunks of four. Each 4KB page from PMM can hold four 1KB L2 tables.
 * This pool manages those pages and tracks which of the four slots are in use with a bitmap.
 */
#ifndef L2_POOL_H
#define L2_POOL_H

#include "stddef.h"
#include "stdint.h"

// An entry in the L2 pool linked list, representing one 4KB page that can hold four 1KB L2 tables.
typedef struct l2_pool_entry
{
    uintptr_t page_pa; // PA of the 4KB page from PMM
    uint8_t used_mask; // bits 0-3: which 1KB slots are occupied
    struct l2_pool_entry *next;
} l2_pool_entry_t;

/**
 * Allocate a 1KB L2 page table from the pool. 
 * @return Address of the allocated L2 table.
 */
uintptr_t l2_pool_alloc(void);

/**
 * Free a previously allocated 1KB L2 page table back to the pool.
 * @param l2_pa PhysAddr of the L2 table to free.
 */
void l2_pool_free(uintptr_t l2_pa);

#endif
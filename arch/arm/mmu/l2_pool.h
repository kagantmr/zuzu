/**
 * @file
 * @brief Allocator for 1 KiB L2 page tables, four to a page.
 */

#ifndef ARCH_ARM_L2_POOL_H
#define ARCH_ARM_L2_POOL_H

#include <stddef.h>
#include <stdint.h>
#include <types.h>

/** @brief A page carved into L2 tables. */
typedef struct L2PtPoolEntry {
    uintptr_t page_pa;          /**< Physical address of the page. */
    uint8_t used_mask;          /**< One bit per table slot in use. */
    struct L2PtPoolEntry *next; /**< Next page in the pool. */
} L2PtPoolEntry;

/**
 * @brief Allocates a zeroed L2 table.
 *
 * @return Its physical address, or 0 when out of memory.
 */
uintptr_t L2PtPoolAlloc(void);

/**
 * @brief Frees an L2 table; the page goes back to the PMM once all its tables are free.
 *
 * @param[in] l2_pa Address from L2PtPoolAlloc(); 0 is ignored.
 */
void L2PtPoolFree(PhysAddr l2_pa);

#endif

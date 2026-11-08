/**
 * @file
 * @brief ARMv7-A cache maintenance by virtual address.
 */

#include <arch/barrier.h>
#include <arch/cache.h>
#include <types.h>

#define CACHE_LINE 64u /**< D-cache line size the range operations step by. */

void ArchCacheCleanDcacheRangePou(VirtAddr start, size_t size)
{
    VirtAddr addr = start & ~(CACHE_LINE - 1);
    VirtAddr end = start + size;
    for (; addr < end; addr += CACHE_LINE)
        __asm__ volatile("mcr p15, 0, %0, c7, c11, 1" ::"r"(addr));
    Dsb();
}

void ArchCacheInvalidateIcacheAll(void)
{
    __asm__ volatile("mcr p15, 0, %0, c7, c5, 0" ::"r"(0u));
    __asm__ volatile("mcr p15, 0, %0, c7, c5, 6" ::"r"(0u));
    ArchSyncBarrier();
}

void ArchCacheCleanInvalidateDcacheRange(VirtAddr start, size_t size)
{
    VirtAddr addr = start & ~(CACHE_LINE - 1);
    VirtAddr end = start + size;
    for (; addr < end; addr += CACHE_LINE)
        __asm__ volatile("mcr p15, 0, %0, c7, c14, 1" ::"r"(addr));
    Dsb();
}

void ArchCacheInvalidateDcacheRange(VirtAddr start, size_t size)
{
    VirtAddr addr = start & ~(CACHE_LINE - 1);
    VirtAddr end = start + size;
    for (; addr < end; addr += CACHE_LINE)
        __asm__ volatile("mcr p15, 0, %0, c7, c6, 1" ::"r"(addr));
    Dsb();
}

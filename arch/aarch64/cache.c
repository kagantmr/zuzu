/**
 * @file
 * @brief AArch64 cache maintenance by virtual address.
 */

#include <arch/barrier.h>
#include <arch/cache.h>
#include <types.h>

#define CACHE_LINE ((VirtAddr)64) /**< D-cache line size the range operations step by. */

void ArchCacheCleanDcacheRangePou(VirtAddr start, size_t size)
{
    VirtAddr addr = start & ~(CACHE_LINE - 1);
    VirtAddr end = start + size;
    for (; addr < end; addr += CACHE_LINE)
        __asm__ volatile("dc cvau, %0" ::"r"(addr) : "memory");
    Dsb();
}

void ArchCacheInvalidateIcacheAll(void)
{
    __asm__ volatile("ic iallu" ::: "memory");
    ArchSyncBarrier();
}

void ArchCacheCleanInvalidateDcacheRange(VirtAddr start, size_t size)
{
    VirtAddr addr = start & ~(CACHE_LINE - 1);
    VirtAddr end = start + size;
    for (; addr < end; addr += CACHE_LINE)
        __asm__ volatile("dc civac, %0" ::"r"(addr) : "memory");
    Dsb();
}

void ArchCacheInvalidateDcacheRange(VirtAddr start, size_t size)
{
    VirtAddr addr = start & ~(CACHE_LINE - 1);
    VirtAddr end = start + size;
    for (; addr < end; addr += CACHE_LINE)
        __asm__ volatile("dc ivac, %0" ::"r"(addr) : "memory");
    Dsb();
}

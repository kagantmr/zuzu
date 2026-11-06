/**
 * @file
 * @brief AArch64 cache maintenance by virtual address.
 */

#include <arch/barrier.h>
#include <arch/cache.h>
#include <types.h>

#define CACHE_LINE ((uintptr_t)64) /**< D-cache line size the range operations step by. */

void ArchCacheCleanDcacheRangePou(uintptr_t start, size_t size)
{
    uintptr_t addr = start & ~(CACHE_LINE - 1);
    uintptr_t end = start + size;
    for (; addr < end; addr += CACHE_LINE)
        __asm__ volatile("dc cvau, %0" ::"r"(addr) : "memory");
    ArchDsb();
}

void ArchCacheInvalidateIcacheAll(void)
{
    __asm__ volatile("ic iallu" ::: "memory");
    ArchSyncBarrier();
}

void ArchCacheCleanInvalidateDcacheRange(uintptr_t start, size_t size)
{
    uintptr_t addr = start & ~(CACHE_LINE - 1);
    uintptr_t end = start + size;
    for (; addr < end; addr += CACHE_LINE)
        __asm__ volatile("dc civac, %0" ::"r"(addr) : "memory");
    ArchDsb();
}

void ArchCacheInvalidateDcacheRange(uintptr_t start, size_t size)
{
    uintptr_t addr = start & ~(CACHE_LINE - 1);
    uintptr_t end = start + size;
    for (; addr < end; addr += CACHE_LINE)
        __asm__ volatile("dc ivac, %0" ::"r"(addr) : "memory");
    ArchDsb();
}

// cache.c - Cache management functions for ARMv7-A

#include <arch/barrier.h>
#include <arch/cache.h>
#include <types.h>

#define CACHE_LINE 64u

void ArchCacheCleanDcacheRangePou(uintptr_t start, size_t size)
{
    uintptr_t addr = start & ~(CACHE_LINE - 1);
    uintptr_t end = start + size;
    for (; addr < end; addr += CACHE_LINE)
        __asm__ volatile("mcr p15, 0, %0, c7, c11, 1" ::"r"(addr)); // DCCMVAU
    ArchDsb(); // put data sync barrier for pipeline to wait
}

void ArchCacheInvalidateIcacheAll(void)
{
    __asm__ volatile("mcr p15, 0, %0, c7, c5, 0" ::"r"(0u)); // ICIALLU
    __asm__ volatile("mcr p15, 0, %0, c7, c5, 6" ::"r"(0u)); // BPIALL
    ArchSyncBarrier();
}

void ArchCacheCleanInvalidateDcacheRange(uintptr_t start, size_t size)
{
    uintptr_t addr = start & ~(CACHE_LINE - 1);
    uintptr_t end = start + size;
    for (; addr < end; addr += CACHE_LINE)
        __asm__ volatile("mcr p15, 0, %0, c7, c14, 1" ::"r"(addr)); // DCCIMVAC
    ArchDsb(); // put data sync barrier for pipeline to wait
}

void ArchCacheInvalidateDcacheRange(uintptr_t start, size_t size)
{
    uintptr_t addr = start & ~(CACHE_LINE - 1);
    uintptr_t end = start + size;
    for (; addr < end; addr += CACHE_LINE)
        __asm__ volatile("mcr p15, 0, %0, c7, c6, 1" ::"r"(addr)); // DCIMVAC
    ArchDsb();
}

// cache.c - Cache management functions for ARMv7-A

#include <arch/cache.h>

#define CACHE_LINE 64

void arch_cache_clean_dcache_range(uintptr_t start, size_t size)
{
    uintptr_t addr = start & ~(CACHE_LINE - 1);
    uintptr_t end = start + size;
    for (; addr < end; addr += CACHE_LINE)
        __asm__ volatile("mcr p15, 0, %0, c7, c11, 1" ::"r"(addr)); // flush out d-cache
    __asm__ volatile("dsb" ::: "memory");                           // put data sync barrier for pipeline to wait
}

void arch_cache_invalidate_icache_range(uintptr_t start, size_t size)
{
    uintptr_t addr = start & ~(CACHE_LINE - 1);
    uintptr_t end = start + size;
    for (; addr < end; addr += CACHE_LINE)
        __asm__ volatile("mcr p15, 0, %0, c7, c5, 1" ::"r"(addr));
    __asm__ volatile("dsb" ::: "memory");
    __asm__ volatile("isb" ::: "memory");
}

void arch_cache_flush_code_range(uintptr_t start, size_t size)
{
    arch_cache_clean_dcache_range(start, size);
    arch_cache_invalidate_icache_range(start, size);
}
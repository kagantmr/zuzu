// arch/cache.h - Neutral cache-maintenance contract.
//
// Used around code loading (ELF) and DMA-visible memory to keep the I/D caches
// coherent with main memory.

#ifndef ZUZU_ARCH_CACHE_H
#define ZUZU_ARCH_CACHE_H

#include <stddef.h>
#include <stdint.h>

/** Clean (write back) D-cache lines covering [start, start+size). */
void arch_cache_clean_dcache_range(uintptr_t start, size_t size);

/** Invalidate I-cache lines covering [start, start+size). */
void arch_cache_invalidate_icache_range(uintptr_t start, size_t size);

/** Make freshly written code at [start, start+size) executable
 *  (clean D-cache + invalidate I-cache). */
void arch_cache_flush_code_range(uintptr_t start, size_t size);

#endif // ZUZU_ARCH_CACHE_H

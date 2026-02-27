#ifndef ARCH_ARM_CACHE_H
#define ARCH_ARM_CACHE_H

#include <stddef.h>
#include <stdint.h>

/* Clean D-cache line to Point of Unification (DCCMVAU) */
/* Invalidate I-cache line to PoU (ICIMVAU) */
/* Full I-cache invalidate (ICIALLU) */

void cache_clean_dcache_range(uintptr_t start, size_t size);
void cache_invalidate_icache_range(uintptr_t start, size_t size);
void cache_flush_code_range(uintptr_t start, size_t size);

#endif // ARCH_ARM_CACHE_H
// arch/cache.h - Neutral cache-maintenance contract.
//
// Used around code loading (ZXF) and DMA-visible memory to keep the I/D caches
// coherent with main memory.

#ifndef ZUZU_ARCH_CACHE_H
#define ZUZU_ARCH_CACHE_H

#include <stddef.h>
#include <stdint.h>

/** Clean (write back) D-cache lines covering [start, start+size). */
void ArchCacheCleanDcacheRange(uintptr_t start, size_t size);

/** Invalidate the whole I-cache (and branch predictor) to the point of
 *  unification. Address-independent, so it is the safe counterpart when the
 *  code being published is only reachable through a kernel alias. */
void ArchCacheInvalidateIcacheAll(void);

#endif // ZUZU_ARCH_CACHE_H

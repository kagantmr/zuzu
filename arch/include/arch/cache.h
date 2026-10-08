// arch/cache.h - Neutral cache-maintenance contract.
//
// Used around code loading (ZXF) and DMA-visible memory to keep the I/D caches
// coherent with main memory.

#ifndef ARCH_CACHE_H
#define ARCH_CACHE_H

#include <stddef.h>
#include <stdint.h>

/** Clean (write back) D-cache lines to PoU covering [start, start+size). */
void ArchCacheCleanDcacheRangePou(uintptr_t start, size_t size);

/** Invalidate the whole I-cache (and branch predictor) to the point of
 *  unification. Address-independent, so it is the safe counterpart when the
 *  code being published is only reachable through a kernel alias. */
void ArchCacheInvalidateIcacheAll(void);

/** Clean and invalidate D-cache lines to PoC covering [start, start+size). */
void ArchCacheCleanInvalidateDcacheRange(uintptr_t start, size_t size);

/** Invalidate D-cache lines to PoC covering [start, start+size). Discards dirty
 *  data, so the range must be cache-line aligned. */
void ArchCacheInvalidateDcacheRange(uintptr_t start, size_t size);

#endif // ARCH_CACHE_H

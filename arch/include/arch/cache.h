/**
 * @file cache.h
 * @brief Definition of cache functions.
 */

#ifndef ARCH_CACHE_H
#define ARCH_CACHE_H

#include <stddef.h>
#include <stdint.h>
#include <types.h>

/**
 * @brief Clean the D-Cache range to the Point of Unity (PoU).
 * 
 * @param[in] start The start address to wipe from the cache.
 * @param[in] size The size of the block the wipe from the cache.
 */
void ArchCacheCleanDcacheRangePou(VirtAddr start, size_t size);

/**
 * @brief Invalidate all of the I-cache.
 */
void ArchCacheInvalidateIcacheAll(void);

/**
 * @brief Clean the D-Cache range to the Point of Coherence (PoC).
 * 
 * @param[in] start The start address to wipe from the cache.
 * @param[in] size The size of the block the wipe from the cache.
 */
void ArchCacheCleanInvalidateDcacheRange(VirtAddr start, size_t size);

/**
 * @brief Invalidate the D-Cache range.
 * 
 * @param[in] start The start address to wipe from the cache.
 * @param[in] size The size of the block the wipe from the cache.
 */
void ArchCacheInvalidateDcacheRange(VirtAddr start, size_t size);

#endif // ARCH_CACHE_H

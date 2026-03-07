#ifndef ARCH_ARM_CACHE_H
#define ARCH_ARM_CACHE_H

#include <stddef.h>
#include <stdint.h>

/**
 *
 * @param start Start of the D-cache line to invalidate.
 * @param size Size of the D-cache line to invalidate.
 */
void cache_clean_dcache_range(uintptr_t start, size_t size);

/**
 *
* @param start Start of the I-cache line to invalidate.
 * @param size Size of the I-cache line to invalidate.
 */
void cache_invalidate_icache_range(uintptr_t start, size_t size);

/**
 *
* @param start Start of code range to invalidate.
 * @param size Size of code range to invalidate.
 */
void cache_flush_code_range(uintptr_t start, size_t size);

#endif // ARCH_ARM_CACHE_H
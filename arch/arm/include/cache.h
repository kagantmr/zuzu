// cache.h - ARM cache maintenance operations
// This file defines functions for cleaning and invalidating the ARM CPU caches.
// ELF loading, MMU management, and other low-level operations may need to perform cache maintenance to ensure memory consistency.

#ifndef ARCH_ARM_CACHE_H
#define ARCH_ARM_CACHE_H

#include <stddef.h>
#include <stdint.h>

/**
 * Clean D-cache lines in the specified range. This writes back any dirty cache lines to memory but does not invalidate them.
 * This is typically used before making memory visible to other agents (e.g., DMA) or before executing code that was just written to memory.
 * @param start Start of the D-cache line to invalidate.
 * @param size Size of the D-cache line to invalidate.
 */
void cache_clean_dcache_range(uintptr_t start, size_t size);

/**
 * Invalidate I-cache lines in the specified range. This forces the CPU to re-cache instructions from memory.
 * This is typically used after writing code to memory (e.g., during ELF loading) to ensure that the CPU executes the updated code.
 * @param start Start of the I-cache line to invalidate.
 * @param size Size of the I-cache line to invalidate.
 */
void cache_invalidate_icache_range(uintptr_t start, size_t size);

/**
 * Flush a range of memory from the D-cache and invalidate the corresponding I-cache lines. This is a common operation after loading code into memory.
 * It ensures that any modified data in the D-cache is written back to memory and that the CPU will fetch the latest instructions from memory.
 * @param start Start of code range to invalidate.
 * @param size Size of code range to invalidate.
 */
void cache_flush_code_range(uintptr_t start, size_t size);

#endif // ARCH_ARM_CACHE_H
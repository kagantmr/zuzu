#ifndef ARCH_ATOMIC_H
#define ARCH_ATOMIC_H

/**
 * This file provides atomic synchronization primitives for ARM architecture using LDREX/STREX instructions.
 * These functions can be used to implement higher-level synchronization primitives like mutexes or spinlocks.
 */

#include <stdint.h>

/**
 * @brief Atomically load a 32-bit value from memory.
 * @param addr Pointer to the memory location to load from.
 * @return The value loaded from memory.
 */
static inline uint32_t arch_ldrex(volatile uint32_t *addr)
{
    uint32_t val;
    __asm__ volatile(
        "ldrex %0, [%1]\n"
        : "=r"(val)
        : "r"(addr)
        : "memory");
    return val;
}

/**
 * @brief Atomically store a 32-bit value to memory if the location has not been modified since the last LDREX.
 * @param addr Pointer to the memory location to store to.
 * @param val The value to store.
 * @return 0 if the store was successful, non-zero if the location was modified by another processor since the last LDREX.
 */
static inline uint32_t arch_strex(volatile uint32_t *addr, uint32_t val)
{
    uint32_t result;
    __asm__ volatile(
        "strex %0, %2, [%1]\n"
        : "=&r"(result)
        : "r"(addr), "r"(val)
        : "memory");
    return result;
}

/**
 * @brief Clear the exclusive monitor, typically called after a failed STREX or when abandoning an atomic operation.
 * This ensures that subsequent LDREX/STREX sequences will not be affected by previous failed attempts.
 */
static inline void arch_clrex(void)
{
    __asm__ volatile("clrex\n" ::: "memory");
}

#endif // ARCH_ATOMIC_H
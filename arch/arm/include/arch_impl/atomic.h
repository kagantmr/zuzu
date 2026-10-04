// arch_impl/atomic.h - ARM atomic primitives via LDREX/STREX (architecture-private).
//
// Do not include directly from neutral code; include <arch/atomic.h> instead.

#ifndef ZUZU_ARM_IMPL_ATOMIC_H
#define ZUZU_ARM_IMPL_ATOMIC_H

#include <stdint.h>

/** Atomically load a 32-bit value, tagging the exclusive monitor. */
static inline uint32_t ArchLoadExclusive(volatile uint32_t *addr)
{
    uint32_t val;
    __asm__ volatile(
        "ldrex %0, [%1]\n"
        : "=r"(val)
        : "r"(addr)
        : "memory");
    return val;
}

/** Conditionally store; returns 0 on success, non-zero if the monitor was lost. */
static inline uint32_t ArchStoreExclusive(volatile uint32_t *addr, uint32_t val)
{
    uint32_t result;
    __asm__ volatile(
        "strex %0, %2, [%1]\n"
        : "=&r"(result)
        : "r"(addr), "r"(val)
        : "memory");
    return result;
}

#endif // ZUZU_ARM_IMPL_ATOMIC_H

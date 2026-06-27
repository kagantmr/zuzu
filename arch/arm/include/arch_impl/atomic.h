// arch_impl/atomic.h - ARM atomic primitives via LDREX/STREX (architecture-private).
//
// Do not include directly from neutral code; include <arch/atomic.h> instead.

#ifndef ZUZU_ARM_IMPL_ATOMIC_H
#define ZUZU_ARM_IMPL_ATOMIC_H

#include <stdint.h>

/** Atomically load a 32-bit value, tagging the exclusive monitor. */
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

/** Conditionally store; returns 0 on success, non-zero if the monitor was lost. */
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

/** Clear the exclusive monitor (after a failed/abandoned STREX). */
static inline void arch_clrex(void)
{
    __asm__ volatile("clrex\n" ::: "memory");
}

static inline int atomic_cas(volatile uint32_t *ptr, uint32_t expected, uint32_t desired) {
    uint32_t tmp, result;
    __asm__ volatile(
        "1: ldrex  %0, [%2]\n"
        "   cmp    %0, %3\n"
        "   bne    2f\n"
        "   strex  %1, %4, [%2]\n"
        "   cmp    %1, #0\n"
        "   bne    1b\n"
        "2:"
        : "=&r"(tmp), "=&r"(result)
        : "r"(ptr), "r"(expected), "r"(desired)
        : "cc", "memory"
    );
    return (tmp == expected);
}

#endif // ZUZU_ARM_IMPL_ATOMIC_H

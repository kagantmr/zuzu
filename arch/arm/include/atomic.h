#ifndef ARCH_ATOMIC_H
#define ARCH_ATOMIC_H

#include <stdint.h>

static inline uint32_t arch_ldrex(volatile uint32_t *addr)
{
    uint32_t val;
    __asm__ volatile(
        "ldrex %0, [%1]\n"
        : "=r"(val)
        : "r"(addr)
        : "memory"
    );
    return val;
}

/* returns 0 on success, 1 on failure */
static inline uint32_t arch_strex(volatile uint32_t *addr, uint32_t val)
{
    uint32_t result;
    __asm__ volatile(
        "strex %0, %2, [%1]\n"
        : "=&r"(result)
        : "r"(addr), "r"(val)
        : "memory"
    );
    return result;
}

static inline void arch_clrex(void)
{
    __asm__ volatile("clrex\n" ::: "memory");
}

#endif  // ARCH_ATOMIC_H
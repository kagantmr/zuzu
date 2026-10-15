#ifndef ARCH_ARM_IMPL_ATOMIC_H
#define ARCH_ARM_IMPL_ATOMIC_H

#include <stdint.h>

static inline uint32_t ArchLoadExclusive(volatile uint32_t *addr)
{
    uint32_t val;
    __asm__ volatile("ldrex %0, [%1]\n" : "=r"(val) : "r"(addr) : "memory");
    return val;
}

static inline uint32_t ArchStoreExclusive(volatile uint32_t *addr, uint32_t val)
{
    uint32_t result;
    __asm__ volatile("strex %0, %2, [%1]\n" : "=&r"(result) : "r"(addr), "r"(val) : "memory");
    return result;
}

#endif // ARCH_ARM_IMPL_ATOMIC_H

#ifndef ARCH_ARM_IMPL_COUNTER_H
#define ARCH_ARM_IMPL_COUNTER_H

#include <stdint.h>

static inline __attribute__((always_inline)) uint64_t ArchCounterRead(void)
{
    uint64_t v;
    __asm__ volatile("isb\n mrrc p15, 1, %Q0, %R0, c14" : "=r"(v)::"memory");
    return v;
}

#endif // ARCH_ARM_IMPL_COUNTER_H

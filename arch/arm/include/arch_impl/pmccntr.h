#ifndef ARCH_ARM_IMPL_PMCCNTR_H
#define ARCH_ARM_IMPL_PMCCNTR_H

#include <arch/barrier.h>
#include <types.h>

static inline __attribute__((always_inline)) uint32_t ArchMeasure(void)
{
    uint32_t cycles;
    ArchIsb();
    __asm__ volatile("mrc p15, 0, %0, c9, c13, 0" : "=r"(cycles) : : "memory");
    ArchIsb();
    return cycles;
}

#endif

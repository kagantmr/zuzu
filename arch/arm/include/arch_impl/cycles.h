/**
 * @file
 * @brief ARMv7-A cycle counter.
 */

#ifndef ARCH_ARM_IMPL_CYCLES_H
#define ARCH_ARM_IMPL_CYCLES_H

#include <arch/barrier.h>
#include <stdint.h>

/**
 * @brief Reads the PMU cycle counter (PMCCNTR), fenced by isb on both sides.
 *
 * @return The current cycle count; wraps at 2^32.
 */
static inline __attribute__((always_inline)) uint32_t ArchMeasure(void)
{
    uint32_t cycles;
    ArchIsb();
    __asm__ volatile("mrc p15, 0, %0, c9, c13, 0" : "=r"(cycles) : : "memory");
    ArchIsb();
    return cycles;
}

#endif // ARCH_ARM_IMPL_CYCLES_H

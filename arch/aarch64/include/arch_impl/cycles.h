/**
 * @file
 * @brief AArch64 cycle counter.
 */

#ifndef ARCH_AARCH64_IMPL_CYCLES_H
#define ARCH_AARCH64_IMPL_CYCLES_H

#include <arch/barrier.h>
#include <arch_impl/sysreg.h>
#include <stdint.h>

/**
 * @brief Reads the PMU cycle counter (PMCCNTR_EL0), fenced by isb on both sides.
 *
 * @return The current cycle count; wraps at 2^32.
 */
static inline __attribute__((always_inline)) uint32_t ArchMeasure(void)
{
    Isb();
    uint32_t cycles = (uint32_t)READ_SYSREG(pmccntr_el0);
    Isb();
    return cycles;
}

#endif // ARCH_AARCH64_IMPL_CYCLES_H

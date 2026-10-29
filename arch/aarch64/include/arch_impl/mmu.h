/**
 * @file
 * @brief ARMv7-A MMU helpers used while switching to the higher-half mapping.
 */

#ifndef ARCH_ARM_IMPL_MMU_H
#define ARCH_ARM_IMPL_MMU_H

#include <stddef.h>

/**
 * @brief Moves the stacks of every exception mode by @p offset.
 *
 * Adds @p offset to sp and fp in SVC mode and to sp in IRQ, abort and undefined
 * modes, with IRQs and FIQs masked, then returns to the calling mode.
 *
 * @param[in] offset Byte distance from the old (physical) to the new (virtual) addresses.
 */
static inline void ArchRelocateStacks(size_t offset)
{
    __asm__ volatile(
        "mrs    r0, cpsr\n\t"
        "mov    r4, r0\n\t"

        "orr    r0, r0, #0xC0\n\t"
        "msr    cpsr_c, r0\n\t"

        "add    sp, sp, %0\n\t"
        "add    fp, fp, %0\n\t"

        "cps    #0x12\n\t"
        "add    sp, sp, %0\n\t"

        "cps    #0x17\n\t"
        "add    sp, sp, %0\n\t"

        "cps    #0x1B\n\t"
        "add    sp, sp, %0\n\t"

        "cps    #0x13\n\t"

        "msr    cpsr_c, r4\n\t"

        :
        : "r"(offset)
        : "r0", "r4", "memory");
}

#endif // ARCH_ARM_IMPL_MMU_H

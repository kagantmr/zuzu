// arch_impl/cpu.h - ARM CPU / interrupt-flag control (architecture-private).
//
// Do not include directly from neutral code; include <arch/cpu.h> instead.

#ifndef ZUZU_ARM_IMPL_CPU_H
#define ZUZU_ARM_IMPL_CPU_H

#include <stdint.h>

/** Disable global IRQs (cpsid i). */
static inline void arch_global_irq_disable(void) {
    __asm__ volatile("cpsid i" ::: "memory");
}

/** Enable global IRQs (cpsie i). */
static inline void arch_global_irq_enable(void) {
    __asm__ volatile("cpsie i" ::: "memory");
}

/**
 * Save CPSR and disable IRQs, returning the previous state for restoration.
 * @return The previous CPSR value before disabling IRQs.
 */
static inline uint32_t arch_irq_save(void) {
    uint32_t cpsr;
    __asm__ volatile("mrs %0, cpsr" : "=r"(cpsr) :: "memory");
    __asm__ volatile("cpsid i" ::: "memory");
    return cpsr;
}

/**
 * Restore the CPSR to re-enable IRQs if they were previously enabled.
 * @param state The CPSR value to restore, typically from arch_irq_save().
 */
static inline void arch_irq_restore(uint32_t state) {
    __asm__ volatile("msr cpsr_c, %0" :: "r"(state) : "memory");
}

#endif // ZUZU_ARM_IMPL_CPU_H

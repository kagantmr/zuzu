// arch_impl/cpu.h - ARM CPU / interrupt-flag control (architecture-private).
//
// Do not include directly from neutral code; include <arch/cpu.h> instead.

#ifndef ARCH_ARM_IMPL_CPU_H
#define ARCH_ARM_IMPL_CPU_H

#include <stdint.h>

typedef uint32_t Cpsr;

/** Disable global IRQs (cpsid i). */
static inline void ArchGlobalIrqDisable(void) {
    __asm__ volatile("cpsid i" ::: "memory");
}

/** Enable global IRQs (cpsie i). */
static inline void ArchGlobalIrqEnable(void) {
    __asm__ volatile("cpsie i" ::: "memory");
}

/**
 * Save Cpsr and disable IRQs, returning the previous state for restoration.
 * @return The previous Cpsr value before disabling IRQs.
 */
static inline Cpsr ArchIrqSave(void) {
    uint32_t cpsr;
    __asm__ volatile("mrs %0, cpsr" : "=r"(cpsr) :: "memory");
    __asm__ volatile("cpsid i" ::: "memory");
    return cpsr;
}

/**
 * Restore the Cpsr to re-enable IRQs if they were previously enabled.
 * @param state The Cpsr value to restore, typically from ArchIrqSave().
 */
static inline void ArchIrqRestore(Cpsr state) {
    __asm__ volatile("msr cpsr_c, %0" :: "r"(state) : "memory");
}

#endif // ARCH_ARM_IMPL_CPU_H

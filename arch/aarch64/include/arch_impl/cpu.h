/**
 * @file
 * @brief AArch64 CPU control.
 */

#ifndef ARCH_AARCH64_IMPL_CPU_H
#define ARCH_AARCH64_IMPL_CPU_H

#include <arch_impl/sysreg.h>
#include <stdbool.h>
#include <stdint.h>
#include <types.h>

/** @brief Masks IRQs on this CPU (msr daifset, #2). */
static inline void ArchGlobalIrqDisable(void) { __asm__ volatile("msr daifset, #2" ::: "memory"); }

/** @brief Unmasks IRQs on this CPU (msr daifclr, #2). */
static inline void ArchGlobalIrqEnable(void) { __asm__ volatile("msr daifclr, #2" ::: "memory"); }

/**
 * @brief Checks whether an IRQ is waiting, even while IRQs are masked (ISR_EL1.I).
 *
 * @return true if an IRQ is pending.
 */
static inline bool ArchIrqPending(void) { return (READ_SYSREG(isr_el1) & (1U << 7)) != 0; }

/** @brief Waits for an interrupt (wfi). */
static inline void ArchCpuIdle(void) { __asm__ volatile("wfi" ::: "memory"); }

/** @brief Loops on wfi forever. Does not mask interrupts itself. */
static inline _Noreturn void ArchCpuHalt(void)
{
    for (;;)
        __asm__ volatile("wfi" ::: "memory");
}

/**
 * @brief Reads the stack pointer of the current mode.
 *
 * @return The current sp.
 */
static inline VirtAddr ArchCurrentStackPointer(void)
{
    VirtAddr sp;
    __asm__ volatile("mov %0, sp" : "=r"(sp));
    return sp;
}

#endif // ARCH_AARCH64_IMPL_CPU_H

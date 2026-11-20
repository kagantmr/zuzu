/**
 * @file
 * @brief ARMv7-A CPU control.
 */

#ifndef ARCH_ARM_IMPL_CPU_H
#define ARCH_ARM_IMPL_CPU_H

#include <stdbool.h>
#include <stdint.h>
#include <types.h>

/** @brief Masks IRQs on this CPU (cpsid i). */
static inline void ArchGlobalIrqDisable(void) { __asm__ volatile("cpsid i" ::: "memory"); }

/** @brief Unmasks IRQs on this CPU (cpsie i). */
static inline void ArchGlobalIrqEnable(void) { __asm__ volatile("cpsie i" ::: "memory"); }

/**
 * @brief Checks whether an IRQ is waiting, even while IRQs are masked (ISR.I).
 *
 * @return true if an IRQ is pending.
 */
static inline bool ArchIrqPending(void)
{
    uint32_t isr;
    __asm__ volatile("mrc p15, 0, %0, c12, c1, 0" : "=r"(isr));
    return (isr & (1U << 7)) != 0;
}

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

#endif // ARCH_ARM_IMPL_CPU_H

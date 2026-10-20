/**
 * @file
 * @brief ARMv7-A CPU control.
 */

#ifndef ARCH_ARM_IMPL_CPU_H
#define ARCH_ARM_IMPL_CPU_H

#include <stdint.h>

/** @brief A saved CPSR, as returned by ArchIrqSave(). */
typedef uint32_t Cpsr;

/** @brief Masks IRQs on this CPU (cpsid i). */
static inline void ArchGlobalIrqDisable(void) { __asm__ volatile("cpsid i" ::: "memory"); }

/** @brief Unmasks IRQs on this CPU (cpsie i). */
static inline void ArchGlobalIrqEnable(void) { __asm__ volatile("cpsie i" ::: "memory"); }

/**
 * @brief Masks IRQs and returns the previous CPSR, for ArchIrqRestore().
 *
 * @return The CPSR before masking.
 */
static inline Cpsr ArchIrqSave(void)
{
    uint32_t cpsr;
    __asm__ volatile("mrs %0, cpsr" : "=r"(cpsr)::"memory");
    __asm__ volatile("cpsid i" ::: "memory");
    return cpsr;
}

/**
 * @brief Restores the IRQ mask saved by ArchIrqSave().
 *
 * @param[in] state Value returned by ArchIrqSave().
 */
static inline void ArchIrqRestore(Cpsr state)
{
    __asm__ volatile("msr cpsr_c, %0" ::"r"(state) : "memory");
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
static inline uintptr_t ArchCurrentStackPointer(void)
{
    uintptr_t sp;
    __asm__ volatile("mov %0, sp" : "=r"(sp));
    return sp;
}

#endif // ARCH_ARM_IMPL_CPU_H

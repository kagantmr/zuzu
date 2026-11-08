/**
 * @file
 * @brief AArch64 CPU control.
 */

#ifndef ARCH_AARCH64_IMPL_CPU_H
#define ARCH_AARCH64_IMPL_CPU_H

#include <arch_impl/sysreg.h>
#include <stdint.h>
#include <types.h>

/** @brief A saved DAIF, as returned by ArchIrqSave(). */
typedef uint64_t IrqState;

/** @brief Masks IRQs on this CPU (msr daifset, #2). */
static inline void ArchGlobalIrqDisable(void) { __asm__ volatile("msr daifset, #2" ::: "memory"); }

/** @brief Unmasks IRQs on this CPU (msr daifclr, #2). */
static inline void ArchGlobalIrqEnable(void) { __asm__ volatile("msr daifclr, #2" ::: "memory"); }

/**
 * @brief Masks IRQs and returns the previous DAIF, for ArchIrqRestore().
 *
 * @return DAIF before masking.
 */
static inline IrqState ArchIrqSave(void)
{
    uint64_t daif = READ_SYSREG(daif);
    ArchGlobalIrqDisable();
    return daif;
}

/**
 * @brief Restores the IRQ mask saved by ArchIrqSave().
 *
 * @param[in] state Value returned by ArchIrqSave().
 */
static inline void ArchIrqRestore(IrqState state)
{
    WRITE_SYSREG(daif, state);
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

#endif // ARCH_AARCH64_IMPL_CPU_H

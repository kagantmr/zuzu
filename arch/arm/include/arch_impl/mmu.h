// arch_impl/mmu.h - ARM MMU inline helpers (architecture-private).
//
// Do not include directly from neutral code; include <arch/mmu.h> instead,
// which pulls this in for the inline bits.

#ifndef ZUZU_ARM_IMPL_MMU_H
#define ZUZU_ARM_IMPL_MMU_H

#include <stddef.h>

/**
 * Relocate the per-mode kernel stacks by a fixed offset. MUST be inlined into
 * its caller (early VMM bring-up): it adjusts SP/FP of the current frame in
 * place while walking the SVC/IRQ/ABT/UND banked stack pointers, so it cannot
 * be a normal (frame-creating) function call.
 */
static inline void arch_relocate_stacks(size_t offset)
{
    __asm__ volatile(
        // Save current mode (should be SVC)
        "mrs    r0, cpsr\n\t"
        "mov    r4, r0\n\t" // r4 = saved CPSR

        // Disable IRQ/FIQ during mode switches (safety)
        "orr    r0, r0, #0xC0\n\t" // Set I and F bits
        "msr    cpsr_c, r0\n\t"

        // --- Relocate SVC stack (current mode) ---
        "add    sp, sp, %0\n\t"
        "add    fp, fp, %0\n\t"

        // --- Switch to IRQ mode and relocate ---
        "cps    #0x12\n\t" // IRQ mode
        "add    sp, sp, %0\n\t"

        // --- Switch to ABT mode and relocate ---
        "cps    #0x17\n\t" // Abort mode
        "add    sp, sp, %0\n\t"

        // --- Switch to UND mode and relocate ---
        "cps    #0x1B\n\t" // Undefined mode
        "add    sp, sp, %0\n\t"

        // --- Return to SVC mode ---
        "cps    #0x13\n\t" // Back to SVC

        // Restore original CPSR (re-enables interrupts if they were enabled)
        "msr    cpsr_c, r4\n\t"

        :
        : "r"(offset)
        : "r0", "r4", "memory");
}

#endif // ZUZU_ARM_IMPL_MMU_H

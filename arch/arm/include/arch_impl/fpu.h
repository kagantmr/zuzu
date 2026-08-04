// arch_impl/fpu.h - ARM VFP lazy-switch implementation (architecture-private).
//
// Do not include directly from neutral code; include <arch/fpu.h> instead.
//
// FPU access is gated by CPACR (cp15 c1,c0,2) bits [23:20], the CP11/CP10
// access-control fields. Clearing them makes any VFP/NEON instruction raise
// an undefined-instruction exception instead of executing, which is how
// arch/arm/exceptions/exception.c catches first-use-after-switch.

#ifndef ZUZU_ARM_IMPL_FPU_H
#define ZUZU_ARM_IMPL_FPU_H

#include <stdint.h>

// 32 double-word VFP registers (d0-d31) + FPSCR. vldmia/vstmia require a
// word-aligned base address; a plain uint8_t array has alignment 1, which
// lets the compiler pack it at an odd offset next to preceding narrow
// fields (see thread_t::fpu_state) and fault on first use.
typedef uint8_t FpuState[32 * 8 + 4] __attribute__((aligned(8)));

// Implemented in arch/arm/vfp.S.
void arch_fpu_save(FpuState *state);
void arch_fpu_restore(const FpuState *state);

static inline void arch_fpu_trap_disable(void)
{
    uint32_t cpacr;
    __asm__ volatile("mrc p15, 0, %0, c1, c0, 2" : "=r"(cpacr));
    cpacr &= ~(0xFu << 20);
    __asm__ volatile("mcr p15, 0, %0, c1, c0, 2" :: "r"(cpacr));
    __asm__ volatile("isb" ::: "memory");
}

static inline void arch_fpu_trap_enable(void)
{
    uint32_t cpacr;
    __asm__ volatile("mrc p15, 0, %0, c1, c0, 2" : "=r"(cpacr));
    cpacr |= (0xFu << 20);
    __asm__ volatile("mcr p15, 0, %0, c1, c0, 2" :: "r"(cpacr));
    __asm__ volatile("isb" ::: "memory");
}

#endif // ZUZU_ARM_IMPL_FPU_H

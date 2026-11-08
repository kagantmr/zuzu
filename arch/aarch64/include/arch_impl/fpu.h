/**
 * @file
 * @brief AArch64 FP/SIMD state and access control.
 */

#ifndef ARCH_AARCH64_IMPL_FPU_H
#define ARCH_AARCH64_IMPL_FPU_H

#include <arch/barrier.h>
#include <arch_impl/sysreg.h>
#include <stdint.h>

/** @brief CPACR_EL1.FPEN, bits 21:20: 0b11 lets EL0 and EL1 use FP/SIMD, 0b00 traps both. */
#define CPACR_EL1_FPEN (3ULL << 20)

/** @brief Saved FP/SIMD state: q0-q31 (512 bytes), then FPCR and FPSR (4 bytes each). */
typedef uint8_t FpuState[(32 * 16) + 8] __attribute__((aligned(16)));

/**
 * @brief Saves the FP/SIMD registers.
 *
 * @param[out] state Where to store them.
 */
void FpuSaveState(FpuState *state);
/**
 * @brief Loads the FP/SIMD registers.
 *
 * @param[in] state State saved by FpuSaveState().
 */
void FpuRestoreState(const FpuState *state);

/**
 * @brief Clears CPACR_EL1.FPEN, so the next FP/SIMD instruction traps with EC 0x07.
 *
 * Used for lazy FPU switching: the trap handler then loads the running task's state.
 */
static inline void ArchFpuTrapDisable(void)
{
    WRITE_SYSREG(cpacr_el1, READ_SYSREG(cpacr_el1) & ~CPACR_EL1_FPEN);
    Isb();
}

/** @brief Sets CPACR_EL1.FPEN, so FP/SIMD instructions run without trapping. */
static inline void ArchFpuEnableAccess(void)
{
    WRITE_SYSREG(cpacr_el1, READ_SYSREG(cpacr_el1) | CPACR_EL1_FPEN);
    Isb();
}

/** @brief Clears FP/SIMD access like ArchFpuTrapDisable(), without the isb; the next exception return synchronizes it. */
static inline void ArchFpuTrapDisableNoSync(void)
{
    WRITE_SYSREG(cpacr_el1, READ_SYSREG(cpacr_el1) & ~CPACR_EL1_FPEN);
}

/** @brief Grants FP/SIMD access like ArchFpuEnableAccess(), without the isb; the next exception return synchronizes it. */
static inline void ArchFpuEnableAccessNoSync(void)
{
    WRITE_SYSREG(cpacr_el1, READ_SYSREG(cpacr_el1) | CPACR_EL1_FPEN);
}

#endif // ARCH_AARCH64_IMPL_FPU_H

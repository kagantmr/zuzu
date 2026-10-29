/**
 * @file
 * @brief ARMv7-A VFP state and access control.
 */

#ifndef ARCH_ARM_IMPL_FPU_H
#define ARCH_ARM_IMPL_FPU_H

#include <stdint.h>

/** @brief Saved VFP state: d0-d31 (256 bytes) followed by FPSCR. */
typedef uint8_t FpuState[32 * 8 + 4] __attribute__((aligned(8)));

/**
 * @brief Saves the VFP registers.
 *
 * @param[out] state Where to store them.
 */
void ArchFpuSaveState(FpuState *state);
/**
 * @brief Loads the VFP registers.
 *
 * @param[in] state State saved by ArchFpuSaveState().
 */
void ArchFpuRestoreState(const FpuState *state);

/**
 * @brief Removes cp10/cp11 access in CPACR, so the next VFP instruction traps.
 *
 * Used for lazy VFP switching: the undefined-instruction handler then loads the
 * running task's state.
 */
static inline void ArchFpuTrapDisable(void)
{
    uint32_t cpacr;
    __asm__ volatile("mrc p15, 0, %0, c1, c0, 2" : "=r"(cpacr));
    cpacr &= ~(0xFu << 20);
    __asm__ volatile("mcr p15, 0, %0, c1, c0, 2" ::"r"(cpacr));
    __asm__ volatile("isb" ::: "memory");
}

/** @brief Grants full cp10/cp11 access in CPACR. */
static inline void ArchFpuEnableAccess(void)
{
    uint32_t cpacr;
    __asm__ volatile("mrc p15, 0, %0, c1, c0, 2" : "=r"(cpacr));
    cpacr |= (0xFu << 20);
    __asm__ volatile("mcr p15, 0, %0, c1, c0, 2" ::"r"(cpacr));
    __asm__ volatile("isb" ::: "memory");
}

#endif // ARCH_ARM_IMPL_FPU_H

#ifndef ARCH_ARM_IMPL_FPU_H
#define ARCH_ARM_IMPL_FPU_H

#include <stdint.h>

typedef uint8_t FpuState[32 * 8 + 4] __attribute__((aligned(8)));

void ArchFpuSaveState(FpuState *state);
void ArchFpuRestoreState(const FpuState *state);

static inline void ArchFpuTrapDisable(void)
{
    uint32_t cpacr;
    __asm__ volatile("mrc p15, 0, %0, c1, c0, 2" : "=r"(cpacr));
    cpacr &= ~(0xFu << 20);
    __asm__ volatile("mcr p15, 0, %0, c1, c0, 2" ::"r"(cpacr));
    __asm__ volatile("isb" ::: "memory");
}

static inline void ArchFpuEnableAccess(void)
{
    uint32_t cpacr;
    __asm__ volatile("mrc p15, 0, %0, c1, c0, 2" : "=r"(cpacr));
    cpacr |= (0xFu << 20);
    __asm__ volatile("mcr p15, 0, %0, c1, c0, 2" ::"r"(cpacr));
    __asm__ volatile("isb" ::: "memory");
}

#endif // ARCH_ARM_IMPL_FPU_H

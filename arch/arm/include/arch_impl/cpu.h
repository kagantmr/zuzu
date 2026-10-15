#ifndef ARCH_ARM_IMPL_CPU_H
#define ARCH_ARM_IMPL_CPU_H

#include <stdint.h>

typedef uint32_t Cpsr;

static inline void ArchGlobalIrqDisable(void) { __asm__ volatile("cpsid i" ::: "memory"); }

static inline void ArchGlobalIrqEnable(void) { __asm__ volatile("cpsie i" ::: "memory"); }

static inline Cpsr ArchIrqSave(void)
{
    uint32_t cpsr;
    __asm__ volatile("mrs %0, cpsr" : "=r"(cpsr)::"memory");
    __asm__ volatile("cpsid i" ::: "memory");
    return cpsr;
}

static inline void ArchIrqRestore(Cpsr state)
{
    __asm__ volatile("msr cpsr_c, %0" ::"r"(state) : "memory");
}

static inline void ArchCpuIdle(void) { __asm__ volatile("wfi" ::: "memory"); }

static inline _Noreturn void ArchCpuHalt(void)
{
    for (;;)
        __asm__ volatile("wfi" ::: "memory");
}

static inline uintptr_t ArchCurrentStackPointer(void)
{
    uintptr_t sp;
    __asm__ volatile("mov %0, sp" : "=r"(sp));
    return sp;
}

#endif // ARCH_ARM_IMPL_CPU_H

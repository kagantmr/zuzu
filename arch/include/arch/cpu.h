#ifndef ARCH_CPU_H
#define ARCH_CPU_H

#include <stdint.h>

#include <arch_impl/cpu.h>

void ArchCpuIdle(void);

_Noreturn void ArchCpuHalt(void);

uintptr_t ArchCurrentStackPointer(void);

#endif // ARCH_CPU_H

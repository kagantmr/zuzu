#ifndef SYS_MM_H
#define SYS_MM_H

#include <arch/regs.h>
#include <stddef.h>

void sys_memmap(CpuState *frame);
void sys_memunmap(CpuState *frame);
void sys_memprotect(CpuState *frame);
void sys_asinject(CpuState *frame);

#endif // SYS_MM_H
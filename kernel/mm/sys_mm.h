#ifndef SYS_MM_H
#define SYS_MM_H

#include <arch/regs.h>
#include <stddef.h>

void sys_memmap(arch_regs_t *frame);
void sys_memunmap(arch_regs_t *frame);
void sys_memprotect(arch_regs_t *frame);
void sys_asinject(arch_regs_t *frame);

#endif // SYS_MM_H
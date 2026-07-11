#ifndef SYS_MM_H
#define SYS_MM_H

#include <arch/regs.h>
#include <stddef.h>

void memmap(arch_regs_t *frame);
void memunmap(arch_regs_t *frame);
void shm_create(arch_regs_t *frame);
void attach(arch_regs_t *frame);
void detach(arch_regs_t *frame);
void mprotect(arch_regs_t *frame);

void asinject(arch_regs_t *frame);

#endif // SYS_MM_H
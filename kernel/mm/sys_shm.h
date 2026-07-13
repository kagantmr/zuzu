#ifndef SYS_SHM_H
#define SYS_SHM_H

#include <arch/regs.h>

void shm_create(arch_regs_t *frame);
void attach(arch_regs_t *frame);
void detach(arch_regs_t *frame);

#endif // SYS_SHM_H
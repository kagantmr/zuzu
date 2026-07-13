#ifndef SYS_SHM_H
#define SYS_SHM_H

#include <arch/regs.h>

void sys_shm_create(arch_regs_t *frame);
void sys_detach(arch_regs_t *frame);

#endif // SYS_SHM_H
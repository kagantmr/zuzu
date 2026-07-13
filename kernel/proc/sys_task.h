#ifndef SYS_TASK_H
#define SYS_TASK_H

#include <arch/regs.h>

void sys_pquit(arch_regs_t *frame);
void sys_yield(arch_regs_t *frame);
void sys_sleep(arch_regs_t *frame);
void sys_getpid(arch_regs_t *frame);
void sys_wait(arch_regs_t *frame);

void sys_pspawn(arch_regs_t *frame);
void sys_kickstart(arch_regs_t *frame);
void sys_pkill(arch_regs_t *frame);

void sys_tmake(arch_regs_t *frame);
void sys_tjoin(arch_regs_t *frame);
void sys_tquit(arch_regs_t *frame);


#endif // SYS_TASK_H
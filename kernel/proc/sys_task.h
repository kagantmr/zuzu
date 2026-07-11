#ifndef SYS_TASK_H
#define SYS_TASK_H

#include <arch/regs.h>

void pquit(arch_regs_t *frame);
void yield(arch_regs_t *frame);
void sleep(arch_regs_t *frame);
void get_pid(arch_regs_t *frame);
void wait(arch_regs_t *frame);

void pspawn(arch_regs_t *frame);
void kickstart(arch_regs_t *frame);
void pkill(arch_regs_t *frame);

void tmake(arch_regs_t *frame);
void tjoin(arch_regs_t *frame);
void tquit(arch_regs_t *frame);


#endif // SYS_TASK_H
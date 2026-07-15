#ifndef SYS_THREAD_H
#define SYS_THREAD_H

#include <arch/regs.h>

void sys_tmake(arch_regs_t *frame);
void sys_tjoin(arch_regs_t *frame);
void sys_tquit(arch_regs_t *frame);


#endif // SYS_THREAD_H
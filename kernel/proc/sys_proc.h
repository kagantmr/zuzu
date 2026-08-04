#ifndef SYS_TASK_H
#define SYS_TASK_H

#include <arch/regs.h>

void sys_pquit(CpuState *frame);
void sys_yield(CpuState *frame);
void sys_sleep(CpuState *frame);
void sys_getpid(CpuState *frame);
void sys_wait(CpuState *frame);

void sys_pspawn(CpuState *frame);
void sys_kickstart(CpuState *frame);
void sys_pkill(CpuState *frame);


#endif // SYS_TASK_H
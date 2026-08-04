#ifndef SYS_THREAD_H
#define SYS_THREAD_H

#include <arch/regs.h>

void sys_tmake(CpuState *frame);
void sys_tjoin(CpuState *frame);
void sys_tquit(CpuState *frame);


#endif // SYS_THREAD_H
#ifndef SYS_THREAD_H
#define SYS_THREAD_H

#include <arch/regs.h>

void SysTMake(CpuState *frame);
void SysTJoin(CpuState *frame);
void SysTQuit(CpuState *frame);


#endif // SYS_THREAD_H
#ifndef SYS_TASK_H
#define SYS_TASK_H

#include <arch/regs.h>

void SysPQuit(CpuState *frame);
void SysYield(CpuState *frame);
void SysSleep(CpuState *frame);
void SysGetPid(CpuState *frame);
void SysWait(CpuState *frame);

void SysPSpawn(CpuState *frame);
void SysKickstart(CpuState *frame);
void SysPKill(CpuState *frame);


#endif // SYS_TASK_H

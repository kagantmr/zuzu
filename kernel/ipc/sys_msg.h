#ifndef SYS_IPC_H
#define SYS_IPC_H

#include <arch/regs.h>


void SysMsgSend(CpuState *frame);
void SysMsgRecv(CpuState *frame);
void SysMsgCall(CpuState *frame);
void SysMsgReply(CpuState *frame);

void SysMsgLsend(CpuState *frame);
void SysMsgLcall(CpuState *frame);
void SysMsgLreply(CpuState *frame);
void SysWaitAny(CpuState *frame);


#endif // SYS_IPC_H
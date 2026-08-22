#ifndef SYS_NTFN_H
#define SYS_NTFN_H

#include <arch/regs.h>
#include <zuzu/types.h>

void SysNtfnCreate(CpuState *frame);
void SysNtfnSignal(CpuState *frame);
void SysNtfnWait(CpuState *frame);


#endif /* SYS_NTFN_H */
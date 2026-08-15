#ifndef SYS_PORT_H
#define SYS_PORT_H

#include <arch/regs.h>

void SysPortCreate(CpuState *frame);
void SysDestroy(CpuState *frame);
void SysGrant(CpuState *frame);
void SysStamp(CpuState *frame);
void SysSetLabel(CpuState *frame);

#endif

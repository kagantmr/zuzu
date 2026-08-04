#ifndef SYS_MM_H
#define SYS_MM_H

#include <arch/regs.h>
#include <stddef.h>

void SysMemMap(CpuState *frame);
void SysMemUnmap(CpuState *frame);
void SysMemProtect(CpuState *frame);
void SysAsInject(CpuState *frame);

#endif // SYS_MM_H
#ifndef KERNEL_SYSPAGE_H
#define KERNEL_SYSPAGE_H

#include <stddef.h>
#include <stdint.h>
#include <types.h>

void SyspageInit(void);
PhysAddr SyspagePa(void);
void SyspageUpdateMem(void);

#endif

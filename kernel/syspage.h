#ifndef KERNEL_SYSPAGE_H
#define KERNEL_SYSPAGE_H

#include <stddef.h>
#include <stdint.h>
#include <types.h>

void SyspageInit(void);                 /* call once at boot after PMM + DTB ready */
PhysAddr SyspagePa(void);               /* returns the physical page address       */
void SyspageUpdateMem(void);            /* call from PMM alloc/free                */
void SyspageUpdateUptime(void);         /* call from tick handler                  */
void SyspageSetInitrdSz(uint32_t size); /* call from initrd setup code           */

#endif

#ifndef KERNEL_SYSPAGE_H
#define KERNEL_SYSPAGE_H

#include <zuzu/types.h>
#include <stddef.h>
#include <stdint.h>

void SyspageInit(void);		      /* call once at boot after PMM + DTB ready */
PhysAddr SyspagePhysAddr(void);	      /* returns the physical page address       */
void SyspageUpdateMem(void);	      /* call from PMM alloc/free                */
void SyspageUpdateUptime(void);	      /* call from tick handler                  */
void SyspageSetInitrdSz(size_t size); /* call from initrd setup code             */

#endif

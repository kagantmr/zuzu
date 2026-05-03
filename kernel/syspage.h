#ifndef KERNEL_SYSPAGE_H
#define KERNEL_SYSPAGE_H

#include <stdint.h>

void     syspage_init(void);        /* call once at boot after PMM + DTB ready */
uintptr_t syspage_pa(void);         /* returns the physical page address       */
void     syspage_update_mem(void);  /* call from PMM alloc/free                */
void     syspage_update_uptime(void); /* call from tick handler                */
void     syspage_set_initrd_size(uint32_t size); /* call from initrd setup code            */

#endif
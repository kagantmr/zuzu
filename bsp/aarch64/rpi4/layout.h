#ifndef RPI4_LAYOUT_H
#define RPI4_LAYOUT_H

#include <arch_impl/user_layout.h>

/* Kernel physical/virtual split.
 * RAM starts at PA 0 on the BCM2711; the firmware loads a 32-bit kernel at
 * 0x8000 by default (see linker.ld BOOT_OFFSET). */
#define KERNEL_PA_BASE 0x00000000UL
#define KERNEL_VA_BASE 0xC0000000UL
#define KERNEL_VA_OFFSET (KERNEL_VA_BASE - KERNEL_PA_BASE)

/* Kernel virtual regions */
#define IOREMAP_BASE 0xF0000000UL
#define IOREMAP_END 0xFFFFFFFFUL
#define KSTACK_REGION_BASE 0xF1000000UL

#endif /* RPI4_LAYOUT_H */

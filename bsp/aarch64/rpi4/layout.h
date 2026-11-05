#ifndef RPI4_LAYOUT_H
#define RPI4_LAYOUT_H

#include <arch_impl/user_layout.h>

/* Kernel physical/virtual split.
 * RAM starts at PA 0 on the BCM2711; the firmware loads a 32-bit kernel at
 * 0x8000 by default (see linker.ld BOOT_OFFSET). */
#define KERNEL_PA_BASE 0x00000000UL
#define KERNEL_VA_BASE 0xFFFF000000000000UL
#define KERNEL_VA_OFFSET (KERNEL_VA_BASE - KERNEL_PA_BASE)

/* Kernel virtual regions */
#define IOREMAP_BASE 0xFFFF800000000000UL
#define KSTACK_REGION_BASE 0xFFFF800040000000UL /* 1 GB of ioremap below the stacks */
#define IOREMAP_END 0xFFFF8FFFFFFFFFFFUL        /* inclusive */

#endif /* RPI4_LAYOUT_H */

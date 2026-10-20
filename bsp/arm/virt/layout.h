#ifndef VIRT_LAYOUT_H
#define VIRT_LAYOUT_H

#include <arch_impl/user_layout.h>

/* Kernel physical/virtual split. QEMU's `virt` machine places RAM at
 * 0x40000000 (vexpress-a15 uses 0x80000000, rpi4 uses 0). There is no
 * on-board firmware for this platform, so it boots the raw image directly
 * via QEMU's -kernel, same as rpi4. */
#define KERNEL_PA_BASE 0x40000000UL
#define KERNEL_VA_BASE 0xC0000000UL
#define KERNEL_VA_OFFSET (KERNEL_VA_BASE - KERNEL_PA_BASE)

/* Kernel virtual regions */
#define IOREMAP_BASE 0xF0000000UL
#define IOREMAP_END 0xFFFFFFFFUL
#define KSTACK_REGION_BASE 0xF1000000UL

#endif /* VIRT_LAYOUT_H */

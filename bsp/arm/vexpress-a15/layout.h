#ifndef VEXPRESS_A15_LAYOUT_H
#define VEXPRESS_A15_LAYOUT_H

#include <arch_impl/user_layout.h>

/* Kernel physical/virtual split */
#define KERNEL_PA_BASE 0x80000000UL
#define KERNEL_VA_BASE 0xC0000000UL
#define KERNEL_VA_OFFSET (KERNEL_VA_BASE - KERNEL_PA_BASE)

/* Kernel virtual regions */
#define IOREMAP_BASE 0xF0000000UL
#define IOREMAP_END 0xFFFFFFFFUL
#define KSTACK_REGION_BASE 0xF1000000UL

#endif /* VEXPRESS_A15_LAYOUT_H */

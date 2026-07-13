#ifndef RPI4_LAYOUT_H
#define RPI4_LAYOUT_H

/* Kernel physical/virtual split.
 * RAM starts at PA 0 on the BCM2711; the firmware loads a 32-bit kernel at
 * 0x8000 by default (see linker.ld BOOT_OFFSET). */
#define KERNEL_PA_BASE    0x00000000UL
#define KERNEL_VA_BASE    0xC0000000UL
#define KERNEL_VA_OFFSET  (KERNEL_VA_BASE - KERNEL_PA_BASE)
#define USER_VA_TOP       0x80000000UL   /* TTBR0/TTBR1 N=1 split */

/* Kernel virtual regions */
#define IOREMAP_BASE         0xF0000000UL
#define IOREMAP_END          0xFFFFFFFFUL
#define KSTACK_REGION_BASE   0xF1000000UL

/* User address space */
#define USER_SYSPAGE_VA      0x00001000UL
#define USER_ELF_BASE        0x00010000UL
#define USER_MMAP_BASE       0x20000000UL
#define USER_DEVICE_BASE     0x7F000000UL
#define USER_IPC_BUF_VA      0x7F7FE000UL
#define USER_DEVICE_LIMIT    USER_IPC_BUF_VA
#define USER_STACK_GUARD_VA  0x7F7FF000UL
#define USER_STACK_BASE      0x7F800000UL   /* 8MB lazy reserve, faulted on demand */
#define USER_STACK_TOP       0x80000000UL
#define USR_SP               0x7FFFE000UL

#endif /* RPI4_LAYOUT_H */

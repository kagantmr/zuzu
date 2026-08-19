ENTRY(_start)

KERNEL_PA_BASE = 0x40000000;        /* Physical RAM base (QEMU `virt`) */
BOOT_OFFSET    = 0x10000;           /* QEMU's raw-kernel load offset for virt */

INCLUDE "arch/arm/common.ld.inc"

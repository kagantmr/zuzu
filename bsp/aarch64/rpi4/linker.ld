ENTRY(_start)

KERNEL_PA_BASE = 0x0000000000000000;        /* Physical RAM base */
BOOT_OFFSET    = 0x80000;            /* Firmware's default load address */

INCLUDE "arch/aarch64/common.ld.inc"

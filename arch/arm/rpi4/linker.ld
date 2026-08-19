ENTRY(_start)

KERNEL_PA_BASE = 0x00000000;        /* Physical RAM base (BCM2711) */
BOOT_OFFSET    = 0x8000;            /* Firmware's default 32-bit load address */

INCLUDE "arch/arm/common.ld.inc"

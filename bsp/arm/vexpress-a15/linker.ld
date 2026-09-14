ENTRY(_start)

KERNEL_PA_BASE = 0x80000000;        /* Physical RAM base */
/* QEMU's own raw-kernel loader for -M vexpress-a15 places (and starts
 * executing) a raw -kernel image at RAM_base + 0x10000, confirmed by
 * reading guest memory at boot — not a value QEMU exposes as configurable,
 * just what its arm_boot code does for this machine. U-Boot's bootm can
 * still load the uImage payload here too; it just needs telling (see
 * UBOOT_KERNEL_LOAD in mk/uboot.mk). */
BOOT_OFFSET    = 0x10000;

INCLUDE "arch/arm/common.ld.inc"

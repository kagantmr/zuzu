# arch/arm/virt/board.mk - QEMU `virt` machine (synthetic, QEMU-only, no
# on-board firmware) board metadata. Included by mk/config.mk once
# BOARD=virt is selected. See arch/arm/arch.mk's header comment for the
# variable list a board.mk may define.

# gic-version=2 is pinned explicitly since it happens to match
# cortex-a15's default here, but a future -cpu change on this board could
# silently switch to a GICv3, which this kernel has no driver for.
DTB_virt        = arch/arm/dtb/virt/virt.dtb
QEMU_MACH_virt  = virt,gic-version=2
QEMU_CPU_virt   = cortex-a15
QEMU_MEM_virt   = 1G
# No SD/MMC controller on this machine -- the SD card image doesn't apply.
QEMU_NO_DRIVE_virt = y

CPUFLAGS_vexpress-a15  = -mcpu=cortex-a15  -falign-functions=64
# No CPUFLAGS_virt override: this board uses ARCH_CPUFLAGS (-mcpu=cortex-a15)
# unmodified.

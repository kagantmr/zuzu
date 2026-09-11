# arch/arm/rpi4/board.mk - Raspberry Pi 4 (BCM2711, Cortex-A72 running
# AArch32) board metadata. Included by mk/config.mk once BOARD=rpi4 is
# selected. See arch/arm/arch.mk's header comment for the variable list a
# board.mk may define.

# DTB is a checked-in compiled blob (matching vexpress-a15/virt), not
# built from rpi4.dts at build time: rpi4.dts itself is hand-edited,
# large, and not the kind of thing worth re-dtc'ing on every build. On
# real hardware the firmware loads bcm2711-rpi-4-b.dtb itself and passes
# its address in r2 -- this DTB is only ever consulted on the QEMU path.
# QEMU's raspi4b machine has fixed 2G RAM and lives in qemu-system-aarch64.
# To regenerate after editing rpi4.dts: `make build/dtb/rpi4.dtb` (still
# wired up in mk/dtb.mk), then copy over arch/arm/dtb/rpi4/bcm2711-rpi-4-b.dtb.
DTB_rpi4        = arch/arm/dtb/rpi4/bcm2711-rpi-4-b.dtb
QEMU_MACH_rpi4  = raspi4b
QEMU_CPU_rpi4   = cortex-a72
QEMU_BIN_rpi4   = qemu-system-aarch64
QEMU_MEM_rpi4   = 2G

# qemu-system-aarch64 additionally refuses 32-bit ELFs outright, so raw
# boot (mk/qemu.mk's default for every board) isn't just preferred here,
# it's the only option.
CPUFLAGS_rpi4   = -mcpu=cortex-a72  -falign-functions=64

# No QEMU path for this board's AArch32 image: qemu-system-arm has neither a
# raspi4b machine nor a cortex-a72, and qemu-system-aarch64's raspi4b boots raw
# images as AArch64 at the arm64 load address, not AArch32 at 0x8000. Boot
# testing happens on real hardware; `make smoke` only checks it builds.
SMOKE_NONE_rpi4 = y
SMOKE_NONE_REASON_rpi4 = QEMU models no AArch32 BCM2711; verify on real hardware

# Boots from its own SD card, so it needs a FAT32 boot partition rather than
# the data-card layout the QEMU boards use. `make BOARD=rpi4 bootfs` stages
# these; `bootimg` wraps them in a FAT32 image. Recursive (=) because IMG,
# INITRD and DTB_FILE are all defined after this file is included.
#
# Filenames here are what the firmware and config.txt look for by name:
# config.txt names kernel=zuzu.img and initramfs initrd.cpio, and the
# firmware loads bcm2711-rpi-4-b.dtb itself.
BOOT_FILES_rpi4  = scripts/rpi4/config.txt $(IMG) $(INITRD) $(DTB_FILE)

# Closed-source Pi firmware (start4.elf, fixup4.dat) is not vendored. Drop it
# here, or leave it on the card and only copy the files above.
BOOT_FW_DIR_rpi4 = firmware

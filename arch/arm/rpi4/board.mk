# arch/arm/rpi4/board.mk - Raspberry Pi 4 (BCM2711, Cortex-A72 running
# AArch32) board metadata. Included by mk/config.mk once BOARD=rpi4 is
# selected. See arch/arm/arch.mk's header comment for the variable list a
# board.mk may define.

# The DTB is compiled from the checked-in rpi4.dts for QEMU; on real
# hardware the firmware loads bcm2711-rpi-4-b.dtb itself and passes its
# address in r2. QEMU's raspi4b machine has fixed 2G RAM and lives in
# qemu-system-aarch64.
DTB_rpi4        = build/dtb/rpi4.dtb
QEMU_MACH_rpi4  = raspi4b
QEMU_CPU_rpi4   = cortex-a72
QEMU_BIN_rpi4   = qemu-system-aarch64
QEMU_MEM_rpi4   = 2G

# qemu-system-aarch64 additionally refuses 32-bit ELFs outright, so raw
# boot (mk/qemu.mk's default for every board) isn't just preferred here,
# it's the only option.
CPUFLAGS_rpi4   = -mcpu=cortex-a72  -falign-functions=64

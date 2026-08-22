# arch/arm/vexpress-a15/board.mk - Versatile Express Cortex-A15 (QEMU-only)
# board metadata. Included by mk/config.mk once BOARD=vexpress-a15 is
# selected. See arch/arm/arch.mk's header comment for the variable list a
# board.mk may define.

DTB_vexpress-a15       = arch/arm/dtb/vexpress-a15/vexpress-v2p-ca15-tc1.dtb
QEMU_MACH_vexpress-a15 = vexpress-a15
QEMU_CPU_vexpress-a15  = cortex-a15
QEMU_NET_vexpress-a15  = -nic user,model=lan9118
# Bare NIC model, reused by the bridged/pcap targets (mk/qemu.mk); keep in
# sync with the model name in QEMU_NET_vexpress-a15 above.
QEMU_NIC_MODEL_vexpress-a15 = lan9118
# Matches the committed DTB's memory@80000000 node (reg size 0x40000000 =
# 1GB) — without this, QEMU only backs its qemu.mk default (64M) while the
# kernel's DTB walk at boot believes it has the full 1GB.
QEMU_MEM_vexpress-a15  = 1G
# Opt-in only: exercises a real bootloader handoff via `make run-uboot`.
# Not needed for `make run` — see mk/uboot.mk's header for why.
UBOOT_vexpress-a15     = y

CPUFLAGS_vexpress-a15  = -mcpu=cortex-a15 -falign-functions=64

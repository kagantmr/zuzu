# arch/arm/arch.mk - ARM (AArch32 / ARMv7-A) architecture build settings.
#
# Included by the top-level Makefile after ARCH is resolved. Defines the
# toolchain, code-generation flags, the list of supported boards, and per-board
# metadata (DTB, QEMU machine/cpu). A new ARM board is added by appending to
# BOARDS and providing its DTB_/QEMU_* entries below plus a board directory
# (arch/arm/<board>/ with _start.S, layout.h, linker.ld, platform.c).

# Toolchain (overridable from the environment / command line via CROSS).
ARCH_CROSS    ?= arm-none-eabi-

# Code generation. CPUFLAGS may be overridden per board (see CPUFLAGS_<board>).
ARCH_CPUFLAGS ?= -mcpu=cortex-a15
ARCH_USER_FP  ?= -mfloat-abi=hard -mfpu=vfpv4

# Supported boards for this architecture.
BOARDS = vexpress-a15 rpi4

# ---- Per-board metadata --------------------------------------------------
# DTB_<board>        : device tree blob passed to QEMU / consumed at boot
# QEMU_MACH_<board>  : QEMU -M machine
# QEMU_CPU_<board>   : QEMU -cpu
# QEMU_BIN_<board>   : (optional) QEMU binary, default qemu-system-arm
# QEMU_MEM_<board>   : (optional) QEMU -m, default 64M
# QEMU_NET_<board>   : (optional) QEMU NIC flags, default none
# CPUFLAGS_<board>   : (optional) override ARCH_CPUFLAGS for this board

DTB_vexpress-a15       = arch/arm/dtb/vexpress-a15/vexpress-v2p-ca15-tc1.dtb
QEMU_MACH_vexpress-a15 = vexpress-a15
QEMU_CPU_vexpress-a15  = cortex-a15
QEMU_NET_vexpress-a15  = -nic user,model=lan9118

# Raspberry Pi 4 (BCM2711, Cortex-A72 running AArch32). The DTB is compiled
# from the checked-in rpi4.dts for QEMU; on real hardware the firmware loads
# bcm2711-rpi-4-b.dtb itself and passes its address in r2. QEMU's raspi4b
# machine has fixed 2G RAM and lives in qemu-system-aarch64.
DTB_rpi4        = build/dtb/rpi4.dtb
QEMU_MACH_rpi4  = raspi4b
QEMU_CPU_rpi4   = cortex-a72
QEMU_BIN_rpi4   = qemu-system-aarch64
QEMU_MEM_rpi4   = 2G
# qemu-system-aarch64 rejects 32-bit ELFs; boot the raw image (Linux boot
# protocol: loaded at 0x8000, DTB address in r2 — same contract as the
# Pi firmware).
QEMU_KERNEL_rpi4 = build/zuzu.img
CPUFLAGS_rpi4   = -mcpu=cortex-a72

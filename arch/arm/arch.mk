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
BOARDS = vexpress-a15

# ---- Per-board metadata --------------------------------------------------
# DTB_<board>        : device tree blob passed to QEMU / consumed at boot
# QEMU_MACH_<board>  : QEMU -M machine
# QEMU_CPU_<board>   : QEMU -cpu
# CPUFLAGS_<board>   : (optional) override ARCH_CPUFLAGS for this board

DTB_vexpress-a15       = arch/arm/dtb/vexpress-a15/vexpress-v2p-ca15-tc1.dtb
QEMU_MACH_vexpress-a15 = vexpress-a15
QEMU_CPU_vexpress-a15  = cortex-a15

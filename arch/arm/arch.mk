# arch/arm/arch.mk - ARM (AArch32 / ARMv7-A) architecture build settings.
#
# Included by the top-level Makefile after ARCH is resolved. Defines the
# toolchain, arch-wide default code-generation flags, and the list of
# supported boards. Per-board metadata (DTB, QEMU machine/cpu, CPUFLAGS
# override, ...) lives in each board's own directory, not here — see
# arch/arm/<board>/board.mk, pulled in by mk/config.mk once BOARD is
# resolved. A new ARM board is added by appending to BOARDS below and
# providing a board directory (arch/arm/<board>/ with _start.S, layout.h,
# linker.ld, platform.c, board.mk) — nothing in this file needs to change
# beyond the BOARDS line.

# Toolchain (overridable from the environment / command line via CROSS).
ARCH_CROSS    ?= arm-none-eabi-

# Code generation. CPUFLAGS may be overridden per board (see CPUFLAGS_<board>
# in that board's board.mk).
ARCH_CPUFLAGS ?= -mcpu=cortex-a15
ARCH_USER_FP  ?= -mfloat-abi=hard -mfpu=vfpv4

# Supported boards for this architecture.
BOARDS = vexpress-a15 rpi4 virt

# ---- Per-board metadata (arch/arm/<board>/board.mk) ------------------------
# DTB_<board>        : device tree blob passed to QEMU / consumed at boot
# QEMU_MACH_<board>  : QEMU -M machine
# QEMU_CPU_<board>   : QEMU -cpu
# QEMU_BIN_<board>   : (optional) QEMU binary, default qemu-system-arm
# QEMU_MEM_<board>   : (optional) QEMU -m, default 64M
# QEMU_NET_<board>   : (optional) QEMU NIC flags, default none
# QEMU_NO_DRIVE_<board> : (optional) set to y to skip the SD card -drive
#                       (boards with no matching disk interface)
# CPUFLAGS_<board>   : (optional) override ARCH_CPUFLAGS for this board
# UBOOT_<board>      : (optional) set to y to enable this board's U-Boot
#                       targets (mk/uboot.mk) — off by default; direct boot
#                       (mk/qemu.mk) works on every board without it

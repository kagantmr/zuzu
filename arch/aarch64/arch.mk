# arch/aarch64/arch.mk - AArch64 build settings.

# Toolchain (overridable from the environment / command line via CROSS).
ARCH_CROSS    ?= aarch64-none-elf
ARCH_NEWLIB_TRIPLE ?= aarch64-none-elf

# Code generation. CPUFLAGS may be overridden per board (see CPUFLAGS_<board>
# in that board's board.mk).
ARCH_CPUFLAGS ?= -mcpu=cortex-a72
ARCH_USER_FP  ?= 

ARCH_KERNEL_CFLAGS ?= -mgeneral-regs-only -mstrict-align

# Supported boards for this architecture.
BOARDS = rpi4

# ---- Per-board metadata (arch/arm/<board>/board.mk) ------------------------
# DTB_<board>        : device tree blob passed to QEMU / consumed at boot
# QEMU_MACH_<board>  : QEMU -M machine
# QEMU_CPU_<board>   : QEMU -cpu
# QEMU_BIN_<board>   : (optional) QEMU binary, default qemu-system-arm
# QEMU_MEM_<board>   : (optional) QEMU -m, default 64M
# QEMU_NET_<board>   : (optional) QEMU NIC flags, default none
# QEMU_NIC_MODEL_<board> : (optional) bare NIC model name for the
#                       bridged/pcap targets (mk/qemu.mk); boards without
#                       one fail loudly on run-bridged/run-pcap instead of
#                       emulating hardware they have no driver for
# QEMU_NO_DRIVE_<board> : (optional) set to y to skip the SD card -drive
#                       (boards with no matching disk interface)
# CPUFLAGS_<board>   : (optional) override ARCH_CPUFLAGS for this board
# UBOOT_<board>      : (optional) set to y to enable this board's U-Boot
#                       targets (mk/uboot.mk): off by default; direct boot
#                       (mk/qemu.mk) works on every board without it

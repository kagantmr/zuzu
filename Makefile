# zuzu top-level build entry point.
#
# This file only resolves ARCH/BOARD and stitches together the modules
# under mk/, each of which owns one concern of the build. Read a module's
# own header comment for what it needs from the ones before it in this
# include chain:
#
#   arch/$(ARCH)/arch.mk  - toolchain prefix, board list, per-board metadata
#   mk/host.mk            - host OS detection, tool-presence checks
#   mk/config.mk          - board validation, build knobs, derived paths
#   mk/toolchain.mk       - CC/LD/AR/... for kernel+tier-1, NEWLIB_CROSS for tier-2
#   mk/user.mk            - user programs: tier-1 (zcrt) + tier-2 (newlib) 
#                           included before kernel.mk, see the comment below
#   mk/kernel.mk          - kernel sources/flags, two-pass symtab link
#   mk/initrd.mk          - initrd.cpio packaging
#   mk/dtb.mk             - .dts -> .dtb, raw kernel IMG
#   mk/sdcard.mk          - SD card FAT32 image workflow
#   mk/qemu.mk            - QEMU run/debug targets
#   mk/uboot.mk           - U-Boot build + uImage + its own run/debug targets
#
# Override ARCH/BOARD on the command line, e.g.
#   make ARCH=arm BOARD=rpi4

ARCH  ?= arm
BOARD ?= vexpress-a15

# Pin the default goal to `all` explicitly. Without this, GNU Make falls
# back to the first target rule encountered anywhere in the include chain
# below (e.g. mk/config.mk's $(BOARD_STAMP) rule) as the default goal, which
# silently breaks a bare `make` — and with it, VSCode Makefile Tools' dry-run
# IntelliSense parse, which builds whatever the default goal is.
.DEFAULT_GOAL := all

include arch/$(ARCH)/arch.mk
include mk/host.mk
include mk/config.mk
include mk/toolchain.mk
# user.mk before kernel.mk: several build/%.o targets are matched by both
# user.mk's tier-specific pattern rules (e.g. build/lib/posix/%.o,
# build/user/newlib_apps/%.o, build/user/%.o) and kernel.mk's blanket
# build/%.o: %.c fallback. When a target's prerequisite exists under more
# than one matching pattern, this make picks whichever pattern was defined
# first rather than the most specific one — so the specific rules must be
# textually defined before the generic one.
include mk/user.mk
include mk/kernel.mk
include mk/initrd.mk
include mk/dtb.mk
include mk/sdcard.mk
include mk/qemu.mk
include mk/uboot.mk

# Default target builds the kernel and every user program across all three
# flag tiers (kernel CC, tier-1 USER_CC, tier-2 NEWLIB_CC), so a plain
# `make` — and therefore VSCode Makefile Tools' dry-run parse of the
# default target — emits a compile command for every source file and
# IntelliSense works without switching targets. `make kernel` (mk/kernel.mk)
# remains for a kernel-only fast iteration loop.
.PHONY: all deploy clean
all: $(TARGET) $(ALL_USER_ELFS) $(SD_LIB_ARCHIVES)

deploy: all sdimg-recreate run

clean:
	@rm -rf build
	@echo "  CLEAN   build"

-include $(DEPS) $(USER_DEPS)

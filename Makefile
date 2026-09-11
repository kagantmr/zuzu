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
#   mk/compile_commands.mk - compile_commands.json for clangd
#
# Override ARCH/BOARD on the command line, e.g.
#   make ARCH=arm BOARD=rpi4

# Make 3.81 (Apple's /usr/bin/make) picks the first-defined matching pattern
# rule instead of the most specific one, which breaks the tier-specific
# build/user/%.o rules against kernel.mk's build/%.o.
ifneq ($(firstword $(sort 4.0 $(MAKE_VERSION))),4.0)
$(error zuzu needs GNU Make >= 4.0, this is $(MAKE_VERSION). On macOS: \
  brew install make, then build with `gmake`)
endif

ARCH  ?= arm
BOARD ?= vexpress-a15

# Otherwise make takes the first rule in the include chain below as the
# default goal, which silently breaks a bare `make`.
.DEFAULT_GOAL := all

include arch/$(ARCH)/arch.mk
include mk/host.mk
include mk/config.mk
include mk/toolchain.mk
include mk/kernel.mk
include mk/user.mk
include mk/initrd.mk
include mk/dtb.mk
include mk/sdcard.mk
include mk/qemu.mk
include mk/uboot.mk
include mk/compile_commands.mk

# Builds the kernel and every user program across all three flag tiers.
# `make kernel` (mk/kernel.mk) is the kernel-only fast iteration loop.
.PHONY: all deploy clean distclean
all: $(TARGET) $(ALL_USER_ELFS) $(SD_LIB_ARCHIVES) $(INITRD) links

deploy: all sdimg-recreate run

# ZUZUSD/ is the pre-$(O) SD staging dir; drop it so old checkouts tidy up.
clean:
	@rm -rf build compile_commands.json ZUZUSD
	@echo "  CLEAN   build compile_commands.json"

distclean: clean
	@rm -rf .baseline .cache
	@echo "  CLEAN   .baseline .cache"

-include $(DEPS) $(USER_DEPS)

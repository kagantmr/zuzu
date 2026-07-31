# mk/toolchain.mk - compiler/linker/binutils selection.
#
# Two distinct toolchains are in play:
#   - CROSS / CC / LD / ...   : kernel + tier-1 user (zcrt). Any
#     arm-none-eabi-gcc works here since zcrt supplies its own libc.
#   - NEWLIB_CROSS / NEWLIB_CC: tier-2 user. Must be a toolchain that
#     actually bundles newlib (see the discovery logic below) since
#     tier-2 programs link against its libc.a.
#
# Requires: arch.mk (ARCH_CROSS) already included.

CROSS   ?= $(ARCH_CROSS)
CC      = $(CROSS)gcc
LD      = $(CC)
OBJDUMP = $(CROSS)objdump
OBJCOPY = $(CROSS)objcopy

USER_CC      = $(CROSS)gcc
USER_LD      = $(USER_CC)
USER_OBJCOPY = $(CROSS)objcopy
USER_AR      = $(CROSS)ar

# ---- tier-2 (newlib) toolchain discovery ---------------------------------
# Homebrew's arm-none-eabi-gcc ships no newlib at all (no libc.a for any
# multilib), so tier-2 needs a toolchain that bundles it. Only search if the
# caller hasn't already pinned NEWLIB_CROSS (env var or `make NEWLIB_CROSS=...`).
ifeq ($(origin NEWLIB_CROSS),undefined)

# A plain arm-none-eabi-gcc on PATH that itself resolves libc.a (Linux's
# gcc-arm-none-eabi apt package bundles newlib this way; Homebrew's doesn't
# and echoes back the bare filename when nothing is found).
_plain_gcc        := $(shell command -v arm-none-eabi-gcc 2>/dev/null)
_plain_libc       := $(if $(_plain_gcc),$(shell $(_plain_gcc) -print-file-name=libc.a),)
_plain_has_newlib := $(filter-out libc.a,$(_plain_libc))

ifneq ($(_plain_has_newlib),)
NEWLIB_CROSS := arm-none-eabi-
else
# Fall back to known Arm GNU Toolchain install locations, newest first.
_agt_bins := $(sort $(wildcard /Applications/ArmGNUToolchain/*/arm-none-eabi/bin/arm-none-eabi-gcc) \
                    $(wildcard /usr/local/arm-gnu-toolchain*/bin/arm-none-eabi-gcc) \
                    $(wildcard /opt/arm-gnu-toolchain*/bin/arm-none-eabi-gcc) \
                    $(wildcard $(HOME)/arm-gnu-toolchain*/bin/arm-none-eabi-gcc))
ifneq ($(_agt_bins),)
NEWLIB_CROSS := $(dir $(lastword $(_agt_bins)))arm-none-eabi-
endif
endif
endif

# Left unset if nothing was found: only an error when a tier-2 target is
# actually requested (see the NEWLIB_CROSS guard in user.mk), so kernel-only
# builds on a machine without a newlib toolchain still work.
NEWLIB_CC = $(NEWLIB_CROSS)gcc
NEWLIB_LD = $(NEWLIB_CC)

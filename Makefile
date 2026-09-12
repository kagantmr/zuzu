# zuzu - top-level build.
#
#   make                     kernel + all user programs
#   make BOARD=virt run      boot under QEMU
#   make smoke / smoke-all   boot and check it came up
#   make -s print-BOARDS     list boards
#
# Layout:
#   scripts/Makefile.lib     host environment, build helpers, compile_commands
#   scripts/Makefile.user    user programs (tier-1 zcrt, tier-2 newlib)
#   scripts/Makefile.image   dtb, raw image, initrd, SD card
#   scripts/Makefile.run     QEMU, smoke, U-Boot
#   arch/$(ARCH)/arch.mk     toolchain prefix, board list
#   arch/$(ARCH)/$(BOARD)/board.mk   per-board metadata

# Make 3.81 (Apple's /usr/bin/make) picks the first-defined matching pattern
# rule instead of the most specific one, and has no $(file).
ifneq ($(firstword $(sort 4.0 $(MAKE_VERSION))),4.0)
$(error zuzu needs GNU Make >= 4.0, this is $(MAKE_VERSION). On macOS: \
  brew install make, then build with `gmake`)
endif

ARCH  ?= arm
BOARD ?= vexpress-a15

# Otherwise make takes the first rule in the include chain as the default goal.
.DEFAULT_GOAL := all

include arch/$(ARCH)/arch.mk
include scripts/Makefile.lib

# ---- configuration -------------------------------------------------------------
ifeq ($(filter $(BOARD),$(BOARDS)),)
$(error unknown BOARD '$(BOARD)' for ARCH '$(ARCH)'; valid boards: $(BOARDS))
endif

# ---- build knobs ------------------------------------------------------------
# Everything configurable lives in Kconfig now; see the CONFIGURATION section
# further down. ANALYZE stays a plain make variable: it is a one-shot report
# mode for `make analyze`, not a property of the kernel being built.
ANALYZE ?= 0

# CONFIG_* may not be set on the command line. kbuild allows it and the result
# is a silent split-brain: the make variable wins in build logic while
# autoconf.h still carries the old value, so the makefiles and the compiled
# code disagree. Fail instead, and say where the knob actually lives.
$(foreach v,$(filter CONFIG_%,$(.VARIABLES)),\
  $(if $(filter command line environment,$(origin $(v))),\
    $(error $(v) cannot be set on the make command line or in the environment. \
      Edit the config instead: make BOARD=$(BOARD) menuconfig, or \
      scripts/config --set $(v)=<value>)))

# ---- derived paths ---------------------------------------------------------
ARCH_DIR       = arch/$(ARCH)
BOARD_DIR      = $(ARCH_DIR)/$(BOARD)

# Board-specific metadata (CPUFLAGS_<board>, DTB_<board>, QEMU_*_<board>,
# UBOOT_<board>, ...) lives in the board's own directory — see
# arch/$(ARCH)/arch.mk's header comment for the variable list a board.mk
# may define. Not -include: a board without one is misconfigured and
# should fail loudly rather than silently fall back to arch-wide defaults.
include $(BOARD_DIR)/board.mk

BOARD_LAYOUT_H = $(BOARD_DIR)/layout.h
LINKER_SCRIPT  = $(BOARD_DIR)/linker.ld
DTB_FILE       = $(DTB_$(BOARD))

# Output tree. Per-board, so switching boards no longer forces a rebuild and
# two boards' objects can't contaminate each other.
O              = build/$(ARCH)-$(BOARD)

MAP            = $(O)/zuzu.map
TARGET         = $(O)/zuzu.elf
# Raw kernel image for real hardware / bootloaders (objcopy -O binary of
# TARGET). Defined here, not next to its build rule in dtb.mk, because it's
# referenced as a *prerequisite* by the U-Boot uImage rule in uboot.mk —
# prerequisite lists expand at parse time (unlike recipe bodies, which
# expand lazily at run time), so a forward reference here would silently
# expand to empty and drop from the dependency list.
IMG            = $(O)/zuzu.img

# Board may override the arch-default cpu flags via CPUFLAGS_<board>.
CPUFLAGS = $(if $(CPUFLAGS_$(BOARD)),$(CPUFLAGS_$(BOARD)),$(ARCH_CPUFLAGS))
INCLUDES = -I. -Iinclude -Iarch/include -Iarch/$(ARCH)/include

LTO_FLAG = $(if $(filter y,$(CONFIG_LTO)),-flto=auto)

# ---- configuration (Kconfig) -------------------------------------------------
# .config is per board, under $(O) -- boards are switched constantly here and
# each keeps its own object tree, so a single root .config would just be a
# thing to clobber. Board defaults come from $(BOARD_DIR)/defconfig.
KCONFIG_CONFIG    = $(O)/.config
KCONFIG_DEFCONFIG = $(BOARD_DIR)/defconfig
KCONFIG_DIR       = $(O)/include/config
KCONFIG_AUTOCONF  = $(KCONFIG_DIR)/autoconf.h
KCONFIG_AUTOCONF_MK = $(KCONFIG_DIR)/auto.conf
KCONFIG_FILES     = $(shell find . -name Kconfig -not -path './build/*' 2>/dev/null)
KCONF             = python3 scripts/kconf.py --kconfig Kconfig

# Goals that must not drag in config generation, or that would recurse.
no-config-goals := clean distclean help %config scripts/config rpi4-firmware

ifeq ($(filter $(no-config-goals),$(MAKECMDGOALS)),)
# Hard include, not -include: a generator that silently failed would
# otherwise leave every CONFIG_ empty and build a kernel with every feature
# off -- exactly the class of bug this whole mechanism exists to prevent.
# Make builds the file and re-execs itself when it is missing or stale.
include $(KCONFIG_AUTOCONF_MK)
# Only meaningful once the file exists: on the first pass make has not built
# it yet, keeps parsing, and re-execs afterwards. The check is here to catch a
# file that exists but is empty or truncated, not a missing one.
ifneq ($(wildcard $(KCONFIG_AUTOCONF_MK)),)
ifndef CONFIG_ZUZU_VALID
$(error $(KCONFIG_AUTOCONF_MK) exists but carries no configuration; \
  regenerate it with: make BOARD=$(BOARD) defconfig)
endif
endif
endif

$(KCONFIG_CONFIG):
	@$(KCONF) olddefconfig --config $@ --defconfig $(KCONFIG_DEFCONFIG) --out $(KCONFIG_DIR)

# One recipe, one target: autoconf.h is written by the same invocation but is
# not itself a target, because make before 4.3 has no grouped targets and a
# two-target rule can run twice under -j, racing on the temp files.
$(KCONFIG_AUTOCONF_MK): $(KCONFIG_CONFIG) $(KCONFIG_FILES)
	@$(KCONF) sync --config $(KCONFIG_CONFIG) --out $(KCONFIG_DIR)

$(KCONFIG_AUTOCONF): $(KCONFIG_AUTOCONF_MK) ;

.PHONY: menuconfig defconfig olddefconfig savedefconfig
menuconfig:
	@$(KCONF) menuconfig --config $(KCONFIG_CONFIG) \
	    --defconfig $(KCONFIG_DEFCONFIG) --out $(KCONFIG_DIR)

defconfig:
	@$(KCONF) defconfig --config $(KCONFIG_CONFIG) \
	    --defconfig $(KCONFIG_DEFCONFIG) --out $(KCONFIG_DIR)
	@echo "  CFG     $(BOARD) reset to $(KCONFIG_DEFCONFIG)"

olddefconfig:
	@$(KCONF) olddefconfig --config $(KCONFIG_CONFIG) \
	    --defconfig $(KCONFIG_DEFCONFIG) --out $(KCONFIG_DIR)

# Write the current config back as the board's checked-in default, minimised.
savedefconfig:
	@$(KCONF) savedefconfig --config $(KCONFIG_CONFIG) --defconfig $(KCONFIG_DEFCONFIG)

# Every object depends on this stamp, whose mtime moves only when the flags
# that went into it actually change -- so `make LOG_LEVEL=3` rebuilds, and a
# no-op re-run doesn't. Lazily expanded: CFLAGS et al. are assembled in
# the kernel/user sections below. Quotes are stripped because the
# signature gets embedded in a shell string and CFLAGS carries
# -DBOARD_LAYOUT_H='"..."'; it only has to change, not round-trip.
FLAGS_SIG = $(subst ",,$(subst ',,$(ARCH)|$(BOARD)|$(CROSS)|$(NEWLIB_CROSS)|\
  $(CFLAGS)|$(LDFLAGS)|$(USER_CFLAGS)|$(USER_LDFLAGS)|$(NEWLIB_USER_CFLAGS)|\
  $(NEWLIB_USER_LDFLAGS)))
FLAGS_STAMP = $(O)/.flags

.PHONY: flags-check
$(FLAGS_STAMP): flags-check
	@mkdir -p $(dir $@)
	@printf '%s' '$(FLAGS_SIG)' | cmp -s - $@ 2>/dev/null || \
	    printf '%s' '$(FLAGS_SIG)' > $@

# Record a compile command next to its object for scripts/ccjson.py. $(file)
# writes it without a shell, so CFLAGS' nested -DBOARD_LAYOUT_H='"..."' quoting
# survives verbatim and nothing has to re-derive the flags. Both functions run

# ---- toolchain -----------------------------------------------------------------
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

# ---- kernel --------------------------------------------------------------------
CFLAGS   = -ffreestanding -O$(CONFIG_CC_OPT_LEVEL) $(LTO_FLAG) -fno-omit-frame-pointer \
           -Wall -Wextra -Werror \
           -Wshadow -Wconversion -Wsign-conversion -Wcast-align -Wcast-qual \
           -Wstrict-prototypes -Wmissing-prototypes -Wformat=2 -Wundef \
           -Wvla -Walloca \
           -Wnull-dereference -Wduplicated-cond -Wduplicated-branches -Wlogical-op \
           -fno-common \
           $(CPUFLAGS) $(INCLUDES) -Ivendor/libfdt -MMD -MP \
           -include $(KCONFIG_AUTOCONF) \
           -D__ZUZU__ -DBOARD_LAYOUT_H='"$(BOARD_LAYOUT_H)"' \
           -DZUZU_ELF_PATH='"$(TARGET)"'
LDFLAGS  = -nostdlib -Wl,-T,$(LINKER_SCRIPT) -Wl,-Map=$(MAP) $(LTO_FLAG)

# DEBUG/NDEBUG keep their standard C spelling rather than moving into
# autoconf.h; CONFIG_DEBUG_BUILD is what selects between them.
ifeq ($(CONFIG_DEBUG_BUILD),y)
    CFLAGS += -DDEBUG -DZUZU_BANNER_SHOW_ADDR -g
else
    CFLAGS += -DNDEBUG
endif
ifeq ($(CONFIG_UBSAN),y)
    CFLAGS += -fsanitize=undefined
endif
# Set only by the `analyze` target below, never directly: swaps -Werror for
# -fanalyzer so a flagged bug is reported rather than fatal. Done here rather
# than by handing a sub-make a rendered CFLAGS string, because CFLAGS carries
# -DBOARD_LAYOUT_H='"..."' -- nested quotes that do not survive being embedded
# in a second shell string.
ifneq ($(ANALYZE), 0)
    CFLAGS := $(filter-out -Werror,$(CFLAGS)) -fanalyzer
endif

KERNEL_LIBGCC = $(shell $(CC) $(CPUFLAGS) -print-libgcc-file-name)

# ---- kernel sources --------------------------------------------------------
# Declared by per-directory build.mk manifests (see collect-objs in
# scripts/Makefile.lib), not discovered on disk -- that is what lets a driver
# or subsystem be selected rather than merely present.
#
# klib/ is the freestanding shared library: built here with kernel CFLAGS and
# again into zcrt with USER_CFLAGS (Makefile.user). lib/ is userspace-only.
# Only the selected board's directory is read, so unselected boards never
# contribute objects.
KERNEL_MANIFEST_DIRS = core drivers kernel klib $(ARCH_DIR) $(BOARD_DIR) vendor/libfdt

OBJS := $(addprefix $(O)/,$(call collect-objs,$(KERNEL_MANIFEST_DIRS)))
DEPS := $(OBJS:.o=.d)

# Vendored third-party source: its type/const-correctness is upstream's
# concern, so an upstream refresh stays a re-download rather than a
# warning-fixing merge. The second pair are zuzu TUs that include libfdt.h,
# whose inline helpers trip the same warnings; per-object overrides key off
# the object being compiled, not the headers it pulls in.
CFLAGS_NOVENDOR = $(filter-out -Wconversion -Wsign-conversion -Wcast-qual -Wcast-align,$(CFLAGS))
$(O)/vendor/libfdt/%.o: CFLAGS := $(CFLAGS_NOVENDOR)
$(O)/kernel/boot_info.o: CFLAGS := $(CFLAGS_NOVENDOR)
$(O)/kernel/dev/fdt_wrappers.o: CFLAGS := $(CFLAGS_NOVENDOR)

# ---- compilation rules ------------------------------------------------------
$(O)/%.o: %.c $(FLAGS_STAMP) $(KCONFIG_AUTOCONF)
	@mkdir -p $(dir $@)
	@echo "  CC      $<"
	@$(CC) $(CFLAGS) -c $< -o $@
	@$(call record-cmd,$(CC) $(CFLAGS) -c $< -o $@)

$(O)/%.o: %.S $(FLAGS_STAMP) $(KCONFIG_AUTOCONF)
	@mkdir -p $(dir $@)
	@echo "  AS      $<"
	@$(CC) $(CFLAGS) -x assembler-with-cpp -c $< -o $@
	@$(call record-cmd,$(CC) $(CFLAGS) -x assembler-with-cpp -c $< -o $@)

# Two-pass link: pass 1 exists only to give symbol.py a symbol table to read,
# which pass 2 links in. Safe because ksymtab.c is pure data (no .text), so the
# addresses captured in pass 1 still hold in pass 2.
$(TARGET): $(OBJS) $(LINKER_SCRIPT)
	@mkdir -p $(dir $@)
	@echo "  LD      (pass1) $@"
	@$(LD) $(LDFLAGS) $(OBJS) $(KERNEL_LIBGCC) -o $@
	@echo "  PY      $(O)/ksymtab.c"
	@rm -f $(O)/ksymtab.c
	@python3 scripts/symbol.py --nm $(CROSS)nm $@ $(O)/ksymtab.c
	@echo "  CC      $(O)/ksymtab.o"
	@$(CC) $(CFLAGS) -c $(O)/ksymtab.c -o $(O)/ksymtab.o
	@echo "  LD      (final) $@"
	@$(LD) $(LDFLAGS) $(O)/ksymtab.o $(OBJS) $(KERNEL_LIBGCC) -o $@
	@if [ "$(DEBUG_BUILD)" = "0" ]; then \
		echo "  STRIP   $@"; \
		$(OBJCOPY) --strip-debug $@ $@; \
	fi

# ---- static analysis ------------------------------------------------------
# `make analyze` rebuilds the kernel from scratch with GCC's -fanalyzer
# symbolic-execution pass and captures the full output in $(O)/analyzer.log.
# -Werror is filtered out so analyzer diagnostics (which are noisier and
# more speculative than the normal warning set) never fail the build; this
# target is a report generator, not a gate. Kept separate from the real
# build because -fanalyzer roughly triples compile time. See the ANALYZE
# knob above CFLAGS for why this passes ANALYZE=1 rather than a rendered
# CFLAGS string.
.PHONY: analyze
analyze:
	@$(MAKE) clean
	@mkdir -p $(O)
	@$(MAKE) ANALYZE=1 kernel 2>&1 | tee $(O)/analyzer.log

.PHONY: kernel dump
kernel: $(TARGET) links

dump: $(TARGET)
	@echo "  OBJDUMP $@"
	@$(OBJDUMP) -D $(TARGET) > $(O)/zuzu.dump

include scripts/Makefile.user
include scripts/Makefile.image
include scripts/Makefile.run

# Builds the kernel and every user program across all three flag tiers.
# `make kernel` is the kernel-only fast iteration loop.
.PHONY: all deploy clean distclean
all: $(TARGET) $(ALL_USER_ELFS) $(SD_LIB_ARCHIVES) $(INITRD) links

deploy: all sdimg-recreate run

# ZUZUSD/ was the pre-$(O) SD staging dir; drop it so old checkouts tidy up.
clean:
	@rm -rf build compile_commands.json ZUZUSD
	@echo "  CLEAN   build compile_commands.json"

distclean: clean
	@rm -rf .baseline .cache
	@echo "  CLEAN   .baseline .cache"

-include $(DEPS) $(USER_DEPS)

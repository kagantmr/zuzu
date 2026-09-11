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

# ---- build knobs ----------------------------------------------------------
OPTIMIZATION_LEVEL      ?= 3
USER_OPTIMIZATION_LEVEL ?= s
DEBUG_BUILD             ?= 1
# LTO does cross-TU whole-program inlining at link time, independent of
# OPTIMIZATION_LEVEL: a function with a single call site (e.g. dtb_init(),
# called only from early.c) can get fully inlined and lose its standalone
# symbol even at -O0, making it unbreakpointable. Set LTO=0 for a debug
# build if a function you want to break on has vanished from the disasm.
LTO                     ?= 0
DTB_DEBUG_WALK          ?= 0
EARLY_UART              ?= 0
TIME_MEASURE            ?= 0
PMM_TRACE               ?= 0
UBSAN                   ?= 0
# Set by `make analyze` (as ANALYZE=1) to swap -Werror for -fanalyzer; never
# set this directly. Needs an explicit default like every other knob here --
# left unset, kernel.mk's `ifneq ($(ANALYZE), 0)` would read empty != 0 as
# true and silently run every build in analyzer mode.
ANALYZE                 ?= 0
# PMCCNTR-based min/avg/max instrumentation at fixed measurement points
# (see kernel/bench.h). Off by default: compiled out entirely, zero
# footprint in production builds.
ZUZU_BENCH              ?= 0

$(foreach v,ZUZU_BENCH TIME_MEASURE,\
  $(if $(filter environment%,$(origin $(v))),\
    $(error $(v) is set in your environment ($($(v))) -- this flag must be \
      passed explicitly on the make command line (e.g. make $(v)=1), never \
      inherited from shell state, since it changes generated code on hot \
      paths. Run: unset $(v))))

LOG_LEVEL               ?= 1
PANIC_SECTION_PROCESS   ?= 1
PANIC_SECTION_SCHEDULER ?= 1
PANIC_SECTION_IRQ       ?= 1
PANIC_SECTION_MEMORY    ?= 1

ifeq ($(filter $(LOG_LEVEL),0 1 2 3 4 5),)
$(error LOG_LEVEL must be an integer from 0 to 5)
endif

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

LTO_FLAG = $(if $(filter 1,$(LTO)),-flto=auto)

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
CFLAGS   = -ffreestanding -O$(OPTIMIZATION_LEVEL) $(LTO_FLAG) -fno-omit-frame-pointer \
           -Wall -Wextra -Werror \
           -Wshadow -Wconversion -Wsign-conversion -Wcast-align -Wcast-qual \
           -Wstrict-prototypes -Wmissing-prototypes -Wformat=2 -Wundef \
           -Wvla -Walloca \
           -Wnull-dereference -Wduplicated-cond -Wduplicated-branches -Wlogical-op \
           -fno-common \
           $(CPUFLAGS) $(INCLUDES) -Ivendor/libfdt -Ivendor/lz4 -MMD -MP \
           -D__ZUZU__ -DBOARD_LAYOUT_H='"$(BOARD_LAYOUT_H)"' -DLOG_LEVEL=$(LOG_LEVEL) \
           -DZUZU_ELF_PATH='"$(TARGET)"'
LDFLAGS  = -nostdlib -Wl,-T,$(LINKER_SCRIPT) -Wl,-Map=$(MAP) $(LTO_FLAG)

ifeq ($(DEBUG_BUILD), 1)
    CFLAGS += -DDEBUG -DZUZU_BANNER_SHOW_ADDR -g
else
    CFLAGS += -DNDEBUG
endif

# Each PANIC_SECTION_<name>=1 knob becomes a -DPANIC_SECTION_<name> define.
CFLAGS += $(foreach s,PROCESS SCHEDULER IRQ MEMORY,\
            $(if $(filter 1,$(PANIC_SECTION_$(s))),-DPANIC_SECTION_$(s)))
ifneq ($(DTB_DEBUG_WALK), 0)
    CFLAGS += -DDTB_DEBUG_WALK
endif
ifneq ($(EARLY_UART), 0)
    CFLAGS += -DEARLY_UART
endif
ifneq ($(TIME_MEASURE), 0)
    CFLAGS += -DTIME_MEASURE
endif
ifneq ($(PMM_TRACE), 0)
    CFLAGS += -DPMM_TRACE
endif
ifneq ($(ZUZU_BENCH), 0)
    CFLAGS += -DZUZU_BENCH
endif
ifneq ($(UBSAN), 0)
    CFLAGS += -fsanitize=undefined -DUBSAN
endif
# Set only by the `analyze` target below (as ANALYZE=1 on the sub-make
# command line, never meant to be set directly): swaps -Werror for
# -fanalyzer so a bug the analyzer flags is reported, not fatal. This has
# to happen in-Makefile rather than by handing the sub-make a fully
# pre-rendered CFLAGS string, because CFLAGS already carries
# -DBOARD_LAYOUT_H='"$(BOARD_LAYOUT_H)"' -- nested quotes that don't
# survive being embedded in a second shell string (`CFLAGS="$(CFLAGS)"`
# on a recipe line): the inner quotes get eaten by the outer ones, and
# `#include BOARD_LAYOUT_H` silently resolves wrong. A one-word command-line
# flag has no quoting to lose.
ifneq ($(ANALYZE), 0)
    CFLAGS := $(filter-out -Werror,$(CFLAGS)) -fanalyzer
endif

KERNEL_LIBGCC = $(shell $(CC) $(CPUFLAGS) -print-libgcc-file-name)

# ---- kernel sources --------------------------------------------------------
# Architecture-neutral source roots. klib/ is the freestanding shared
# library (libkern model): compiled here with kernel CFLAGS and again into
# ZCRT with USER_CFLAGS (see user.mk). lib/ is userspace-only libc.
NONARCH_DIRS = core drivers kernel klib

# Within arch/$(ARCH), exclude every board's directory, then add back only
# the selected BOARD_DIR so unselected boards never get compiled in.
ARCH_PRUNE_BOARDS = $(foreach b,$(BOARDS),-not -path '$(ARCH_DIR)/$(b)/*')

LIBFDT_SRCS = \
	vendor/libfdt/fdt.c \
	vendor/libfdt/fdt_ro.c \
	vendor/libfdt/fdt_addresses.c \
	vendor/libfdt/fdt_rw.c \
	vendor/libfdt/fdt_wip.c \
	vendor/libfdt/fdt_strerror.c


# := (not =): these run `find` once at parse time. With recursive (=)
# expansion each reference below would re-run `find` on disk.
CSRCS     := $(shell find $(NONARCH_DIRS) -name '*.c')
CSRCS     += $(shell find $(ARCH_DIR) -name '*.c' $(ARCH_PRUNE_BOARDS))
CSRCS     += $(shell find $(BOARD_DIR) -name '*.c')
CSRCS     += $(LIBFDT_SRCS)

# libfdt is vendored third-party source (see vendor/libfdt/): its
# type/const-correctness is upstream's concern, not zuzu's. Filter the
# noisiest correctness warnings back out for this directory only so an
# upstream refresh stays a re-download, not a warning-fixing merge. Same
# per-object override pattern as vendor/lz4/lz4.o above, widened to the
# directory with a pattern-stem target. -Wcast-align is included alongside
# the -Wconversion/-Wsign-conversion/-Wcast-qual set the task named
# explicitly: libfdt's device-tree walkers cast byte offsets into struct
# pointers throughout, tripping the same "not ours to fix" warning.
$(O)/vendor/libfdt/%.o: CFLAGS := $(filter-out -Wconversion -Wsign-conversion -Wcast-qual -Wcast-align,$(CFLAGS))

# libfdt.h itself is a vendored header, and its inline helpers (byte-store
# accessors, string-length-to-int narrowing, etc.) trip the same warnings
# when the two zuzu TUs that use libfdt directly (rather than compiling
# vendor/libfdt/*.c) pull it in. Same rationale and filter as above, just
# addressed at the including object instead of the vendor directory, since
# per-object CFLAGS overrides key off the object being compiled, not the
# headers it happens to include.
$(O)/kernel/boot_info.o: CFLAGS := $(filter-out -Wconversion -Wsign-conversion -Wcast-qual -Wcast-align,$(CFLAGS))
$(O)/kernel/dev/fdt_wrappers.o: CFLAGS := $(filter-out -Wconversion -Wsign-conversion -Wcast-qual -Wcast-align,$(CFLAGS))
ASRCS_ALL := $(shell find $(NONARCH_DIRS) -name '*.S') \
             $(shell find $(ARCH_DIR) -name '*.S' $(ARCH_PRUNE_BOARDS)) \
             $(shell find $(BOARD_DIR) -name '*.S')
ASRCS     := $(filter-out $(ARCH_DIR)/crt0.S,$(ASRCS_ALL))
OBJS      := $(CSRCS:%.c=$(O)/%.o) $(ASRCS:%.S=$(O)/%.o)
DEPS      := $(OBJS:.o=.d)

# ---- compilation rules ------------------------------------------------------
$(O)/%.o: %.c $(FLAGS_STAMP)
	@mkdir -p $(dir $@)
	@echo "  CC      $<"
	@$(CC) $(CFLAGS) -c $< -o $@
	@$(call record-cmd,$(CC) $(CFLAGS) -c $< -o $@)

$(O)/%.o: %.S $(FLAGS_STAMP)
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

# mk/config.mk - board validation, build knobs, and derived paths.
#
# Requires: arch/$(ARCH)/arch.mk already included (BOARDS, ARCH_CPUFLAGS,
# per-board DTB_/CPUFLAGS_ variables).

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
# kernel.mk/user.mk, after this file. Quotes are stripped because the
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

# build/zuzu.{elf,map,img} -> the current board's real output, for humans and
# muscle memory. Phony so it never goes stale; only links what actually exists,
# so a board that hasn't been img'd doesn't leave a dangling link. Tools that
# care which board they're looking at should use $(O) directly -- these
# repoint on every board switch.
.PHONY: links
links:
	@mkdir -p build
	@for f in zuzu.elf zuzu.map zuzu.img; do \
	    if [ -e $(O)/$$f ]; then ln -sfn $(ARCH)-$(BOARD)/$$f build/$$f; fi; \
	done

# Expose any make variable to scripts: `make -s print-BOARDS`.
.PHONY: print-%
print-%:
	@echo '$($*)'

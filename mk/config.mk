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
LTO                     ?= 1
DTB_DEBUG_WALK          ?= 0
EARLY_UART              ?= 0
TIME_MEASURE            ?= 0
PMM_TRACE               ?= 0
# PMCCNTR-based min/avg/max instrumentation at fixed measurement points
# (see kernel/bench.h). Off by default: compiled out entirely, zero
# footprint in production builds.
ZUZU_BENCH              ?= 0
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
BOARD_LAYOUT_H = $(BOARD_DIR)/layout.h
LINKER_SCRIPT  = $(BOARD_DIR)/linker.ld
DTB_FILE       = $(DTB_$(BOARD))
MAP            = build/zuzu.map
TARGET         = build/zuzu.elf
# Raw kernel image for real hardware / bootloaders (objcopy -O binary of
# TARGET). Defined here, not next to its build rule in dtb.mk, because it's
# referenced as a *prerequisite* by the U-Boot uImage rule in uboot.mk —
# prerequisite lists expand at parse time (unlike recipe bodies, which
# expand lazily at run time), so a forward reference here would silently
# expand to empty and drop from the dependency list.
IMG            = build/zuzu.img

# Board may override the arch-default cpu flags via CPUFLAGS_<board>.
CPUFLAGS = $(if $(CPUFLAGS_$(BOARD)),$(CPUFLAGS_$(BOARD)),$(ARCH_CPUFLAGS))
INCLUDES = -I. -Iinclude -Iarch/include -Iarch/$(ARCH)/include

LTO_FLAG = $(if $(filter 1,$(LTO)),-flto=auto)

# Objects bake in per-board flags (BOARD_LAYOUT_H, CPUFLAGS), so a BOARD
# switch must rebuild everything. The stamp file changes name with the
# board; every object depends on it, forcing a full rebuild when it
# (re)appears.
BOARD_STAMP = build/.board-$(BOARD)

$(BOARD_STAMP):
	@mkdir -p build
	@rm -f build/.board-*
	@touch $@

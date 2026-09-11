# mk/kernel.mk - kernel sources, flags, and the two-pass symtab link.
#
# Requires: config.mk (CPUFLAGS/INCLUDES/LTO_FLAG/dirs), toolchain.mk (CC/LD).

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

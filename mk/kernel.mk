# mk/kernel.mk - kernel sources, flags, and the two-pass symtab link.
#
# Requires: config.mk (CPUFLAGS/INCLUDES/LTO_FLAG/dirs), toolchain.mk (CC/LD).

CFLAGS   = -ffreestanding -O$(OPTIMIZATION_LEVEL) $(LTO_FLAG) -fno-omit-frame-pointer \
           -Wall -Wextra -Werror $(CPUFLAGS) $(INCLUDES) -Ivendor/libfdt -Ivendor/lz4 -MMD -MP \
           -D__KERNEL__ -DBOARD_LAYOUT_H='"$(BOARD_LAYOUT_H)"' -DLOG_LEVEL=$(LOG_LEVEL)
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

LZ4_SRCS = vendor/lz4/lz4.c

# := (not =): these run `find` once at parse time. With recursive (=)
# expansion each reference below would re-run `find` on disk.
CSRCS     := $(shell find $(NONARCH_DIRS) -name '*.c')
CSRCS     += $(shell find $(ARCH_DIR) -name '*.c' $(ARCH_PRUNE_BOARDS))
CSRCS     += $(shell find $(BOARD_DIR) -name '*.c')
CSRCS     += $(LIBFDT_SRCS)
CSRCS     += $(LZ4_SRCS)

# Per-object flags for vendored files that need config zuzu supplies at
# compile time rather than by patching the vendored source (keeps a
# future upstream refresh a re-download, not a merge -- see
# vendor/lz4/VENDOR.md). Isolated to this one object; must never leak into
# the rest of the kernel. The next vendored file that needs its own flags
# gets its own target-specific line here, same pattern.
build/vendor/lz4/lz4.o: CFLAGS += -DLZ4_FREESTANDING=1 -DLZ4_FORCE_MEMORY_ACCESS=0 \
    -DLZ4_memcpy=memcpy -DLZ4_memmove=memmove -DLZ4_memset=memset \
    -include string.h
ASRCS_ALL := $(shell find $(NONARCH_DIRS) -name '*.S') \
             $(shell find $(ARCH_DIR) -name '*.S' $(ARCH_PRUNE_BOARDS)) \
             $(shell find $(BOARD_DIR) -name '*.S')
ASRCS     := $(filter-out $(ARCH_DIR)/crt0.S,$(ASRCS_ALL))
OBJS      := $(CSRCS:%.c=build/%.o) $(ASRCS:%.S=build/%.o)
DEPS      := $(OBJS:.o=.d)

# ---- compilation rules ------------------------------------------------------
build/%.o: %.c $(BOARD_STAMP)
	@mkdir -p $(dir $@)
	@echo "  CC      $<"
	@$(CC) $(CFLAGS) -c $< -o $@

build/%.o: %.S $(BOARD_STAMP)
	@mkdir -p $(dir $@)
	@echo "  AS      $<"
	@$(CC) $(CFLAGS) -x assembler-with-cpp -c $< -o $@

# two-pass build to generate kernel symbol table
$(TARGET): $(OBJS) $(LINKER_SCRIPT)
	@mkdir -p $(dir $@)
	@echo "  LD      (pass1) $@"
	@$(LD) $(LDFLAGS) $(OBJS) $(KERNEL_LIBGCC) -o $@
	@echo "  PY      generating build/ksymtab.c"
	@python3 scripts/symbol.py $@ build/ksymtab.c || true
	@if [ -f build/ksymtab.c ]; then \
		echo "  CC      build/ksymtab.o"; \
		$(CC) $(CFLAGS) -c build/ksymtab.c -o build/ksymtab.o; \
		echo "  LD      (final) $@"; \
		$(LD) $(LDFLAGS) build/ksymtab.o $(OBJS) $(KERNEL_LIBGCC) -o $@; \
	else \
		echo "  WARN: build/ksymtab.c not generated; final ELF uses empty symbol table"; \
	fi
	@if [ "$(DEBUG_BUILD)" = "0" ]; then \
		echo "  STRIP   $@"; \
		$(OBJCOPY) --strip-debug $@ $@; \
	fi

.PHONY: kernel dump
kernel: $(TARGET)

dump: $(TARGET)
	@echo "  OBJDUMP $@"
	@$(OBJDUMP) -D $(TARGET) > build/zuzu.dump

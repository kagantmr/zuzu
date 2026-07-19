# Architecture and board selection. Override on the command line, e.g.
#   make ARCH=arm BOARD=vexpress-a15
ARCH  ?= arm
BOARD ?= vexpress-a15

# Per-architecture build settings (toolchain, cpu flags, board list/metadata).
include arch/$(ARCH)/arch.mk

# Validate the requested board against the arch's supported list.
ifeq ($(filter $(BOARD),$(BOARDS)),)
$(error unknown BOARD '$(BOARD)' for ARCH '$(ARCH)'; valid boards: $(BOARDS))
endif

CROSS   ?= $(ARCH_CROSS)
CC      = $(CROSS)gcc
LD      = $(CC)
OBJDUMP = $(CROSS)objdump
OBJCOPY = $(CROSS)objcopy

UNAME_S := $(shell uname -s)

# ---- build knobs ---------------------------------------------------------
OPTIMIZATION_LEVEL      ?= 2
USER_OPTIMIZATION_LEVEL ?= s
DEBUG_BUILD             ?= 1
DTB_DEBUG_WALK          ?= 0
EARLY_UART              ?= 0
LOG_LEVEL               ?= 1
PANIC_SECTION_PROCESS   ?= 1
PANIC_SECTION_SCHEDULER ?= 1
PANIC_SECTION_IRQ       ?= 1
PANIC_SECTION_MEMORY    ?= 1

ifeq ($(filter $(LOG_LEVEL),0 1 2 3 4 5),)
$(error LOG_LEVEL must be an integer from 0 to 5)
endif

ARCH_DIR       = arch/$(ARCH)
BOARD_DIR      = $(ARCH_DIR)/$(BOARD)
BOARD_LAYOUT_H = $(BOARD_DIR)/layout.h
LINKER_SCRIPT  = $(BOARD_DIR)/linker.ld
DTB_FILE       = $(DTB_$(BOARD))
MAP            = build/zuzu.map
TARGET         = build/zuzu.elf

# ---- kernel flags --------------------------------------------------------
# Board may override the arch-default cpu flags via CPUFLAGS_<board>.
CPUFLAGS = $(if $(CPUFLAGS_$(BOARD)),$(CPUFLAGS_$(BOARD)),$(ARCH_CPUFLAGS))
INCLUDES = -I. -Iinclude -Iarch/include -Iarch/$(ARCH)/include

CFLAGS   = -ffreestanding -O$(OPTIMIZATION_LEVEL) -flto=auto -fno-omit-frame-pointer \
           -Wall -Wextra -Werror $(CPUFLAGS) $(INCLUDES) -Ivendor/libfdt -MMD -MP \
           -D__KERNEL__ -DBOARD_LAYOUT_H='"$(BOARD_LAYOUT_H)"' -DLOG_LEVEL=$(LOG_LEVEL)
LDFLAGS  = -nostdlib -Wl,-T,$(LINKER_SCRIPT) -Wl,-Map=$(MAP) -flto=auto

# ---- user (tier-1, zuzu libc) flags --------------------------------------
USER_CC      = $(CROSS)gcc
USER_LD      = $(USER_CC)
USER_OBJCOPY = $(CROSS)objcopy
KERNEL_LIBGCC = $(shell $(CC) $(CPUFLAGS) -print-libgcc-file-name)
USER_LIBGCC   = $(shell $(USER_CC) $(CPUFLAGS) $(ARCH_USER_FP) -print-libgcc-file-name)

USER_CFLAGS  = -ffreestanding -nostdlib -O$(USER_OPTIMIZATION_LEVEL) -Wall -Wextra \
               $(CPUFLAGS) $(INCLUDES) -MMD -MP -g $(ARCH_USER_FP) \
               -DBOARD_LAYOUT_H='"$(BOARD_LAYOUT_H)"' -DLOG_LEVEL=$(LOG_LEVEL)
USER_LDFLAGS = -nostdlib -Wl,-T,user/user.ld

ifeq ($(DEBUG_BUILD), 1)
    CFLAGS      += -DDEBUG -DZUZU_BANNER_SHOW_ADDR -g
    USER_CFLAGS += -DDEBUG
else
    CFLAGS      += -DNDEBUG
    USER_CFLAGS += -DNDEBUG
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

# ---- tier-2 (newlib) flags -----------------------------------------------
# Homebrew's arm-none-eabi-gcc ships no newlib at all (no libc.a for any
# multilib), so tier-2 compiles and links with the Arm GNU toolchain, which
# bundles it. Override NEWLIB_CROSS if yours lives elsewhere.
NEWLIB_CROSS ?= /Applications/ArmGNUToolchain/15.2.rel1/arm-none-eabi/bin/arm-none-eabi-
NEWLIB_CC = $(NEWLIB_CROSS)gcc
NEWLIB_LD = $(NEWLIB_CC)

# Newlib's headers must win over include/, but <zuzu/...> must still resolve.
# This dir exposes zuzu/ alone. (Goes away with the klib/ split.)
NEWLIB_INC = build/newlib-include

NEWLIB_USER_CFLAGS = -O$(USER_OPTIMIZATION_LEVEL) -Wall -Wextra \
                     $(CPUFLAGS) -I. -I$(NEWLIB_INC) -Iarch/include -Iarch/$(ARCH)/include \
                     -MMD -MP -g $(ARCH_USER_FP) \
                     -DBOARD_LAYOUT_H='"$(BOARD_LAYOUT_H)"'

NEWLIB_USER_LDFLAGS = -nostartfiles -Wl,-T,user/user.ld \
                      -mthumb -mcpu=cortex-a15 -mfloat-abi=hard -mfpu=vfpv4

# ---- user programs -------------------------------------------------------
# Auto-discovered: every directory under a role dir is one program, named
# after its directory (sources inside may nest arbitrarily). ELF/initrd
# naming stays the bare program name regardless of where its sources live.
# Role decides packaging and libc tier:
#   BOOT_ROLES   -> initrd, tier-1
#   DISK_ROLES   -> SD card image, tier-1
#   NEWLIB_ROLES -> SD card image, tier-2 (newlib)
BOOT_ROLES   = services drivers shell
DISK_ROLES   = test_apps
NEWLIB_ROLES = newlib_apps

prog_dirs = $(shell find $(foreach r,$(1),user/$(r)) -mindepth 1 -maxdepth 1 -type d 2>/dev/null)
BOOT_PROG_DIRS   := $(call prog_dirs,$(BOOT_ROLES))
DISK_PROG_DIRS   := $(call prog_dirs,$(DISK_ROLES))
NEWLIB_PROG_DIRS := $(call prog_dirs,$(NEWLIB_ROLES))

BOOT_PROGS   = $(notdir $(BOOT_PROG_DIRS))
DISK_PROGS   = $(notdir $(DISK_PROG_DIRS))
NEWLIB_PROGS = $(notdir $(NEWLIB_PROG_DIRS))
USER_PROGS   = $(BOOT_PROGS) $(DISK_PROGS) $(NEWLIB_PROGS)

$(foreach d,$(BOOT_PROG_DIRS) $(DISK_PROG_DIRS) $(NEWLIB_PROG_DIRS),$(eval USER_DIR_$(notdir $(d)) := $(d)))
$(foreach p,$(USER_PROGS),$(eval USER_$(p)_SRCS := $(shell find $(USER_DIR_$(p)) -name '*.c')))
$(foreach p,$(USER_PROGS),$(eval USER_$(p)_OBJS := $(patsubst user/%.c,build/user/%.o,$(USER_$(p)_SRCS))))
USER_APP_OBJS = $(foreach p,$(USER_PROGS),$(USER_$(p)_OBJS))

# zcrt: the shared user-side runtime (libc + IPC).
ZCRT_SRCS = $(wildcard lib/*.c lib/zuzu/*.c)
ZCRT_OBJS = $(patsubst %.c,build/user/zcrt/%.o,$(ZCRT_SRCS))
# Tier-2 links only the IPC runtime lib/*.c would shadow newlib's libc.
ZCRT_ZUZU_OBJS = $(filter build/user/zcrt/lib/zuzu/%,$(ZCRT_OBJS))
# ...except sbrk: the _sbrk stub calls sbrk() expecting zuzu's arena
# allocator. Without this object the linker pulls newlib's sbrk() from
# libc.a, which calls _sbrk_r -> _sbrk -> sbrk: unbounded recursion that
# runs the stack into the guard page on the first malloc.
ZCRT_SBRK_OBJ = build/user/zcrt/lib/sbrk.o
NEWLIB_STUB_SRCS = $(wildcard user/lib/posix/*.c)
NEWLIB_STUB_OBJS = $(patsubst user/%.c,build/user/%.o,$(NEWLIB_STUB_SRCS))

USER_CRT0             = build/user/crt0.o
NEWLIB_CRT0           = build/user/crt0-newlib.o
BOOT_PROG_PACKED_ELFS = $(BOOT_PROGS:%=build/user/%.stripped.elf)
# Everything staged into the SD image: tier-1 disk apps + tier-2 newlib apps.
SD_PROGS              = $(DISK_PROGS) $(NEWLIB_PROGS)
SD_PROG_PACKED_ELFS   = $(SD_PROGS:%=build/user/%.stripped.elf)

# ---- initrd --------------------------------------------------------------
INITRD             = build/initrd.cpio
INITRD_OBJ         = build/$(ARCH_DIR)/initrd.o
INITRD_EXTRA_DIR  ?= initrd
INITRD_EXTRA_FILES := $(shell find $(INITRD_EXTRA_DIR) -type f 2>/dev/null)

# ---- kernel sources ------------------------------------------------------
# Architecture-neutral source roots.
NONARCH_DIRS = core drivers kernel lib

# Within arch/$(ARCH), exclude every board's directory, then add back only the
# selected BOARD_DIR — so unselected boards never get compiled in.
ARCH_PRUNE_BOARDS = $(foreach b,$(BOARDS),-not -path '$(ARCH_DIR)/$(b)/*')

LIBFDT_SRCS = \
	vendor/libfdt/fdt.c \
	vendor/libfdt/fdt_ro.c \
	vendor/libfdt/fdt_addresses.c \
	vendor/libfdt/fdt_rw.c \
	vendor/libfdt/fdt_wip.c \
	vendor/libfdt/fdt_strerror.c

CSRCS     = $(shell find $(NONARCH_DIRS) -name '*.c')
CSRCS    += $(shell find $(ARCH_DIR) -name '*.c' $(ARCH_PRUNE_BOARDS))
CSRCS    += $(shell find $(BOARD_DIR) -name '*.c')
CSRCS    := $(filter-out lib/zuzu/zmalloc.c,$(CSRCS))
CSRCS    += $(LIBFDT_SRCS)
ASRCS_ALL = $(shell find $(NONARCH_DIRS) -name '*.S') \
            $(shell find $(ARCH_DIR) -name '*.S' $(ARCH_PRUNE_BOARDS)) \
            $(shell find $(BOARD_DIR) -name '*.S')
ASRCS     = $(filter-out $(ARCH_DIR)/crt0.S $(ARCH_DIR)/initrd.S,$(ASRCS_ALL))
OBJS      = $(CSRCS:%.c=build/%.o) $(ASRCS:%.S=build/%.o)
DEPS      = $(OBJS:.o=.d)
USER_DEPS = $(USER_CRT0:.o=.d) $(NEWLIB_CRT0:.o=.d) $(USER_APP_OBJS:.o=.d) $(ZCRT_OBJS:.o=.d) $(NEWLIB_STUB_OBJS:.o=.d)

# sd card image configuration
SD_IMG         ?= build/sd.img
SD_IMG_SIZE_MB ?= 64
SD_VOL_LABEL   ?= ZUZU
SD_STAGE_DIR   ?= ZUZUSD

# default
all: $(TARGET)

.SECONDARY: $(USER_APP_OBJS) $(ZCRT_OBJS)

# Objects bake in per-board flags (BOARD_LAYOUT_H, CPUFLAGS), so a BOARD
# switch must rebuild everything. The stamp file changes name with the board;
# every object depends on it, forcing a full rebuild when it (re)appears.
BOARD_STAMP = build/.board-$(BOARD)

$(BOARD_STAMP):
	@mkdir -p build
	@rm -f build/.board-*
	@touch $@

# ---- compilation rules ---------------------------------------------------
$(NEWLIB_INC)/zuzu:
	@mkdir -p $(NEWLIB_INC)
	@ln -sfn ../../include/zuzu $(NEWLIB_INC)/zuzu

# Must precede the generic build/user/%.o rule: make 3.81 picks the first
# matching pattern rule, not the most specific one.
build/user/newlib_apps/%.o: user/newlib_apps/%.c $(BOARD_STAMP) $(NEWLIB_INC)/zuzu
	@mkdir -p $(dir $@)
	@echo "  CC[nl]  $<"
	@$(NEWLIB_CC) $(NEWLIB_USER_CFLAGS) -c $< -o $@

build/user/lib/posix/%.o: user/lib/posix/%.c $(BOARD_STAMP) $(NEWLIB_INC)/zuzu
	@mkdir -p $(dir $@)
	@echo "  CC[nl]  $<"
	@$(NEWLIB_CC) $(NEWLIB_USER_CFLAGS) -c $< -o $@

build/user/%.o: user/%.c $(BOARD_STAMP)
	@mkdir -p $(dir $@)
	@echo "  CC      $<"
	@$(USER_CC) $(USER_CFLAGS) -c $< -o $@

build/%.o: %.c $(BOARD_STAMP)
	@mkdir -p $(dir $@)
	@echo "  CC      $<"
	@$(CC) $(CFLAGS) -c $< -o $@

build/%.o: %.S $(BOARD_STAMP)
	@mkdir -p $(dir $@)
	@echo "  AS      $<"
	@$(CC) $(CFLAGS) -x assembler-with-cpp -c $< -o $@

build/user/zcrt/%.o: %.c $(BOARD_STAMP)
	@mkdir -p $(dir $@)
	@echo "  CC      $<"
	@$(USER_CC) $(USER_CFLAGS) -c $< -o $@

$(USER_CRT0): $(ARCH_DIR)/crt0.S
	@mkdir -p $(dir $@)
	@echo "  AS      $<"
	@$(USER_CC) $(USER_CFLAGS) -x assembler-with-cpp -c $< -o $@

$(NEWLIB_CRT0): $(ARCH_DIR)/crt0.S
	@mkdir -p $(dir $@)
	@echo "  AS[nl]  $<"
	@$(NEWLIB_CC) $(NEWLIB_USER_CFLAGS) -DZUZU_NEWLIB -x assembler-with-cpp -c $< -o $@

# ---- user program link rules ---------------------------------------------
define LINK_USER_PROG
build/user/$(1).elf: $$(USER_$(1)_OBJS) $(USER_CRT0) $(ZCRT_OBJS) user/user.ld
	@mkdir -p $$(dir $$@)
	@echo "  LD      $$@"
	@$(USER_LD) $(USER_LDFLAGS) $(USER_CRT0) $$(USER_$(1)_OBJS) $(ZCRT_OBJS) $(USER_LIBGCC) -o $$@
endef

$(foreach p,$(BOOT_PROGS) $(DISK_PROGS),$(eval $(call LINK_USER_PROG,$(p))))

define LINK_NEWLIB_PROG
build/user/$(1).elf: $$(USER_$(1)_OBJS) $(NEWLIB_CRT0) $(NEWLIB_STUB_OBJS) $(ZCRT_ZUZU_OBJS) $(ZCRT_SBRK_OBJ) user/user.ld
	@mkdir -p $$(dir $$@)
	@echo "  LD[nl]  $$@"
	@$(NEWLIB_LD) $(NEWLIB_USER_LDFLAGS) $(NEWLIB_CRT0) $$(USER_$(1)_OBJS) $(NEWLIB_STUB_OBJS) $(ZCRT_ZUZU_OBJS) $(ZCRT_SBRK_OBJ) -o $$@
endef

$(foreach p,$(NEWLIB_PROGS),$(eval $(call LINK_NEWLIB_PROG,$(p))))

build/user/%.stripped.elf: build/user/%.elf
	@echo "  STRIP   $@"
	@$(USER_OBJCOPY) --strip-debug $< $@

# ---- initrd + kernel link ------------------------------------------------
$(INITRD): $(BOOT_PROG_PACKED_ELFS) $(INITRD_EXTRA_FILES)
	@rm -rf build/initrd
	@mkdir -p build/initrd/bin
	@for prog in $(BOOT_PROGS); do \
		cp build/user/$$prog.stripped.elf build/initrd/bin/$$prog; \
	done
	@if [ -d "$(INITRD_EXTRA_DIR)" ]; then \
		cp -R $(INITRD_EXTRA_DIR)/. build/initrd/; \
	fi
	@cd build/initrd && find . -not -name '.' | sort | cpio -o -H newc > ../initrd.cpio 2>/dev/null
	@echo "  CPIO    $@ ($(words $(BOOT_PROGS)) boot program(s))"

$(INITRD_OBJ): $(ARCH_DIR)/initrd.S $(INITRD)
	@mkdir -p $(dir $@)
	@echo "  AS      $<"
	@$(CC) $(CFLAGS) -x assembler-with-cpp -c $< -o $@

# two-pass build to generate kernel symbol table
$(TARGET): $(OBJS) $(INITRD_OBJ) $(LINKER_SCRIPT)
	@mkdir -p $(dir $@)
	@echo "  LD      (pass1) $@"
	@$(LD) $(LDFLAGS) $(OBJS) $(INITRD_OBJ) $(KERNEL_LIBGCC) -o $@
	@echo "  PY      generating build/ksymtab.c"
	@python3 scripts/symbol.py $@ build/ksymtab.c || true
	@if [ -f build/ksymtab.c ]; then \
		echo "  CC      build/ksymtab.o"; \
		$(CC) $(CFLAGS) -c build/ksymtab.c -o build/ksymtab.o; \
		echo "  LD      (final) $@"; \
		$(LD) $(LDFLAGS) build/ksymtab.o $(OBJS) $(INITRD_OBJ) $(KERNEL_LIBGCC) -o $@; \
	else \
		echo "  WARN: build/ksymtab.c not generated; final ELF uses empty symbol table"; \
	fi
	@if [ "$(DEBUG_BUILD)" = "0" ]; then \
		echo "  STRIP   $@"; \
		$(OBJCOPY) --strip-debug $@ $@; \
	fi

# ---- SD card workflow ----------------------------------------------------
#
#  Typical workflow:
#    make                  — build kernel + all programs
#    make sdimg            — stage DISK_PROGS and create sd.img from ZUZUSD/
#    make run              — launch QEMU
#
#  ZUZUSD/ is the staging directory tracked in git.
#  Put user data, config files, and non-boot resources there directly.
#  Binaries land in ZUZUSD/bin/ — add that path to .gitignore.
#
#  To update the SD card after code changes:
#    make sdimg-recreate && make run

.PHONY: sdimg sdimg-stage sdimg-clean sdimg-recreate

sdimg-stage: $(SD_PROG_PACKED_ELFS)
	@mkdir -p $(SD_STAGE_DIR)/bin
	@for prog in $(SD_PROGS); do \
		cp build/user/$$prog.stripped.elf $(SD_STAGE_DIR)/bin/$$prog; \
		echo "  STAGE   $(SD_STAGE_DIR)/bin/$$prog"; \
	done

ifeq ($(UNAME_S),Darwin)
$(SD_IMG): sdimg-stage
	@mkdir -p $(dir $(SD_IMG))
	@rm -f $(SD_IMG) $(SD_IMG).dmg
	@echo "  IMG     $(SD_IMG) from $(SD_STAGE_DIR)/ ($(SD_IMG_SIZE_MB)MB, FAT32)"
	@hdiutil create -srcfolder "$(SD_STAGE_DIR)" -fs "MS-DOS FAT32" \
	    -volname $(SD_VOL_LABEL) -size $(SD_IMG_SIZE_MB)m \
	    -format UDIF $(SD_IMG) >/dev/null
	@mv $(SD_IMG).dmg $(SD_IMG)
else
$(SD_IMG): sdimg-stage
	@mkdir -p $(dir $(SD_IMG))
	@rm -f $(SD_IMG)
	@echo "  IMG     $(SD_IMG) from $(SD_STAGE_DIR)/ ($(SD_IMG_SIZE_MB)MB, FAT32)"
	@if ! command -v mkfs.fat >/dev/null 2>&1; then \
	    echo "  IMG     failed: install dosfstools  (sudo apt install dosfstools)"; \
	    exit 1; \
	fi
	@if ! command -v mcopy >/dev/null 2>&1; then \
	    echo "  IMG     failed: install mtools  (sudo apt install mtools)"; \
	    exit 1; \
	fi
	@dd if=/dev/zero of=$(SD_IMG) bs=1M count=$(SD_IMG_SIZE_MB) status=none
	@mkfs.fat -F 32 -n $(SD_VOL_LABEL) $(SD_IMG) >/dev/null
	@mcopy -i $(SD_IMG) -s $(SD_STAGE_DIR)/* ::
endif

sdimg: $(SD_IMG)

sdimg-clean:
	@rm -f $(SD_IMG)
	@echo "  CLEAN   $(SD_IMG)"

sdimg-recreate: sdimg-clean sdimg

# ---- run / debug targets -------------------------------------------------
.PHONY: run run-bridged run-pcap debug

PCAP_FILE ?= /tmp/zuzu.pcap

QEMU_MACHINE = $(QEMU_MACH_$(BOARD))
QEMU_CPU     = $(QEMU_CPU_$(BOARD))
QEMU_BIN     = $(if $(QEMU_BIN_$(BOARD)),$(QEMU_BIN_$(BOARD)),qemu-system-arm)
QEMU_MEM     = $(if $(QEMU_MEM_$(BOARD)),$(QEMU_MEM_$(BOARD)),64M)
# Board NIC flags; empty for boards whose NIC QEMU does not emulate.
QEMU_NET     = $(QEMU_NET_$(BOARD))
# What QEMU boots: the ELF by default, a raw image where the board needs it.
QEMU_KERNEL  = $(if $(QEMU_KERNEL_$(BOARD)),$(QEMU_KERNEL_$(BOARD)),$(TARGET))

# Flags shared by every run/debug variant; each target adds -kernel + extras.
QEMU_ARGS = -M $(QEMU_MACHINE) -cpu $(QEMU_CPU) -m $(QEMU_MEM) \
            -dtb $(DTB_FILE) -nographic -drive file=$(SD_IMG),if=sd,format=raw

run: $(QEMU_KERNEL) $(DTB_FILE)
	@echo "  QEMU    $(QEMU_KERNEL)"
	@$(QEMU_BIN) $(QEMU_ARGS) -kernel $(QEMU_KERNEL) $(QEMU_NET)

run-bridged: $(TARGET) $(DTB_FILE)
	@echo "  QEMU    $(TARGET) [bridged]"
	@sudo $(QEMU_BIN) $(QEMU_ARGS) -kernel $(TARGET) \
	    -nic vmnet-bridged,model=lan9118,ifname=en0,mac=52:54:00:ab:cd:ef

run-pcap: $(TARGET) $(DTB_FILE)
	@echo "  QEMU    $(TARGET) [pcap -> $(PCAP_FILE)]"
	@$(QEMU_BIN) $(QEMU_ARGS) -kernel $(TARGET) \
	    -net nic,model=lan9118 -net user,id=n0 \
	    -object filter-dump,id=f0,netdev=n0,file=$(PCAP_FILE)
	@echo "  PCAP    wrote $(PCAP_FILE) (read with: tcpdump -nr $(PCAP_FILE))"

debug: $(TARGET) $(DTB_FILE)
	@echo "  QEMU    $(TARGET) (debug)"
	@$(QEMU_BIN) $(QEMU_ARGS) -kernel $(TARGET) $(QEMU_NET) -S -gdb tcp::1234

# ---- misc targets --------------------------------------------------------
# Device tree blobs compiled from checked-in sources (QEMU only; real
# hardware boots with the firmware-provided DTB).
build/dtb/%.dtb: %.dts
	@mkdir -p $(dir $@)
	@echo "  DTC     $@"
	@dtc -I dts -O dtb -o $@ $<

# Raw kernel image for real hardware (e.g. kernel= in the Pi 4 config.txt).
IMG = build/zuzu.img

$(IMG): $(TARGET)
	@echo "  OBJCOPY $@"
	@$(OBJCOPY) -O binary $(TARGET) $(IMG)

.PHONY: all dump clean img deploy

img: $(IMG)

dump: $(TARGET)
	@echo "  OBJDUMP $@"
	@$(OBJDUMP) -D $(TARGET) > build/zuzu.dump

deploy: all sdimg-recreate run

clean:
	@rm -rf build
	@echo "  CLEAN   build"

-include $(DEPS) $(USER_DEPS)

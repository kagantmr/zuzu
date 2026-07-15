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

OPTIMIZATION_LEVEL ?= 2
USER_OPTIMIZATION_LEVEL ?= s
DEBUG_BUILD        ?= 1
DTB_DEBUG_WALK     ?= 0
EARLY_UART         ?= 0
LOG_LEVEL          ?= 1
BANNER             ?= 1
PANIC_SECTION_PROCESS    ?= 1
PANIC_SECTION_SCHEDULER  ?= 1
PANIC_SECTION_IRQ        ?= 1
PANIC_SECTION_MEMORY     ?= 1


ARCH_DIR      = arch/$(ARCH)
BOARD_DIR     = $(ARCH_DIR)/$(BOARD)
BOARD_LAYOUT_H = $(BOARD_DIR)/layout.h
LINKER_SCRIPT = $(BOARD_DIR)/linker.ld
DTB_FILE      = $(DTB_$(BOARD))
MAP           = build/zuzu.map

# Board may override the arch-default cpu flags via CPUFLAGS_<board>.
CPUFLAGS = $(if $(CPUFLAGS_$(BOARD)),$(CPUFLAGS_$(BOARD)),$(ARCH_CPUFLAGS))
CFLAGS   = -ffreestanding -O$(OPTIMIZATION_LEVEL) -flto=auto -fno-omit-frame-pointer \
           -Wall -Wextra -Werror $(CPUFLAGS) -I. -Iinclude -Iarch/include -Iarch/$(ARCH)/include -MMD -MP \
           -D__KERNEL__ -DBOARD_LAYOUT_H='"$(BOARD_LAYOUT_H)"'
CFLAGS  += -Ivendor/libfdt
LDFLAGS  = -nostdlib -Wl,-T,$(LINKER_SCRIPT) -Wl,-Map=$(MAP) -flto=auto


ifeq ($(LOG_LEVEL), 0)
	CFLAGS += -DLOG_LEVEL=0
else ifeq ($(LOG_LEVEL), 1)
	CFLAGS += -DLOG_LEVEL=1
else ifeq ($(LOG_LEVEL), 2)
	CFLAGS += -DLOG_LEVEL=2
else ifeq ($(LOG_LEVEL), 3)
	CFLAGS += -DLOG_LEVEL=3
else ifeq ($(LOG_LEVEL), 4)
	CFLAGS += -DLOG_LEVEL=4
else ifeq ($(LOG_LEVEL), 5)
	CFLAGS += -DLOG_LEVEL=5
else
	$(error LOG_LEVEL must be an integer from 0 to 5)
endif
ifeq ($(DEBUG_BUILD), 1)
    CFLAGS += -UNDEBUG -DDEBUG -DZUZU_BANNER_SHOW_ADDR -g
else
    CFLAGS += -DNDEBUG -UDEBUG -UZUZU_BANNER_SHOW_ADDR
endif
ifeq ($(PANIC_SECTION_PROCESS), 1)
    CFLAGS += -DPANIC_SECTION_PROCESS
endif
ifeq ($(PANIC_SECTION_SCHEDULER), 1)
    CFLAGS += -DPANIC_SECTION_SCHEDULER
endif
ifeq ($(PANIC_SECTION_IRQ), 1)
    CFLAGS += -DPANIC_SECTION_IRQ
endif
ifeq ($(PANIC_SECTION_MEMORY), 1)
    CFLAGS += -DPANIC_SECTION_MEMORY
endif
ifeq ($(DTB_DEBUG_WALK), 0)
    CFLAGS += -UDTB_DEBUG_WALK
else
    CFLAGS += -DDTB_DEBUG_WALK
endif
ifeq ($(EARLY_UART), 0)
    CFLAGS += -UEARLY_UART
else
    CFLAGS += -DEARLY_UART
endif

USER_CC      = $(CROSS)gcc
USER_LD      = $(USER_CC)
USER_OBJCOPY = $(CROSS)objcopy
KERNEL_LIBGCC = $(shell $(CC) $(CPUFLAGS) -print-libgcc-file-name)
USER_LIBGCC   = $(shell $(USER_CC) $(CPUFLAGS) $(ARCH_USER_FP) -print-libgcc-file-name)

USER_CFLAGS  = -ffreestanding -nostdlib -O$(USER_OPTIMIZATION_LEVEL) -Wall -Wextra \
			   $(CPUFLAGS) -I. -Iinclude -Iarch/include -Iarch/$(ARCH)/include -MMD -MP -g $(ARCH_USER_FP) \
			   -DBOARD_LAYOUT_H='"$(BOARD_LAYOUT_H)"'
USER_LDFLAGS = -nostdlib -Wl,-T,user/user.ld

ifeq ($(LOG_LEVEL), 0)
	USER_CFLAGS += -DLOG_LEVEL=0
else ifeq ($(LOG_LEVEL), 1)
	USER_CFLAGS += -DLOG_LEVEL=1
else ifeq ($(LOG_LEVEL), 2)
	USER_CFLAGS += -DLOG_LEVEL=2
else ifeq ($(LOG_LEVEL), 3)
	USER_CFLAGS += -DLOG_LEVEL=3
else ifeq ($(LOG_LEVEL), 4)
	USER_CFLAGS += -DLOG_LEVEL=4
else ifeq ($(LOG_LEVEL), 5)
	USER_CFLAGS += -DLOG_LEVEL=5
else
	$(error LOG_LEVEL must be an integer from 0 to 5)
endif

ifeq ($(DEBUG_BUILD), 1)
    USER_CFLAGS += -UNDEBUG -DDEBUG
else
    USER_CFLAGS += -DNDEBUG -UDEBUG
endif

BOOT_PROGS = sysd devmgr pl011drv pl181drv fat32d fbox lan9118drv netd zzsh
DISK_PROGS = test zuzufetch

USER_PROGS = $(BOOT_PROGS) $(DISK_PROGS)

# Program name -> source directory (user/ is grouped by role: services/,
# drivers/, shell/, test_apps/). ELF/initrd naming stays the bare program
# name regardless of where its sources live.
USER_DIR_sysd       = user/services/sysd
USER_DIR_devmgr     = user/services/devmgr
USER_DIR_fbox       = user/services/fbox
USER_DIR_pl011drv   = user/drivers/pl011drv
USER_DIR_pl181drv   = user/drivers/pl181drv
USER_DIR_fat32d     = user/drivers/fat32d
USER_DIR_lan9118drv = user/drivers/lan9118drv
USER_DIR_netd       = user/services/netd
USER_DIR_zzsh       = user/shell/zzsh
USER_DIR_test       = user/test_apps/test
USER_DIR_zuzufetch  = user/test_apps/zuzufetch
USER_DIR_cycletest  = user/test_apps/cycletest

# zcrt + user lib sources
ZCRT_SRCS = $(wildcard lib/*.c lib/zuzu/*.c)
ZCRT_OBJS = $(patsubst %.c,build/user/zcrt/%.o,$(ZCRT_SRCS))

ULIB_SRCS = $(wildcard user/lib/*.c)
ULIB_OBJS = $(patsubst user/%.c,build/user/%.o,$(ULIB_SRCS))

# derived user program sources + objects
USER_CRT0      = build/user/crt0.o
BOOT_PROG_ELFS = $(foreach p,$(BOOT_PROGS),build/user/$(p).elf)
DISK_PROG_ELFS = $(foreach p,$(DISK_PROGS),build/user/$(p).elf)
USER_ELFS      = $(BOOT_PROG_ELFS) $(DISK_PROG_ELFS)
BOOT_PROG_PACKED_ELFS = $(foreach p,$(BOOT_PROGS),build/user/$(p).stripped.elf)
DISK_PROG_PACKED_ELFS = $(foreach p,$(DISK_PROGS),build/user/$(p).stripped.elf)
INITRD         = build/initrd.cpio
INITRD_OBJ     = build/$(ARCH_DIR)/initrd.o
INITRD_EXTRA_DIR ?= initrd
INITRD_EXTRA_FILES := $(shell find $(INITRD_EXTRA_DIR) -type f 2>/dev/null)

$(foreach p,$(USER_PROGS),$(eval USER_$(p)_SRCS := $(shell find $(USER_DIR_$(p)) -name '*.c')))
$(foreach p,$(USER_PROGS),$(eval USER_$(p)_OBJS := $(patsubst user/%.c,build/user/%.o,$(USER_$(p)_SRCS))))
USER_APP_OBJS = $(foreach p,$(USER_PROGS),$(USER_$(p)_OBJS))

# Architecture-neutral source roots.
NONARCH_DIRS = core drivers kernel lib

# Within arch/$(ARCH), exclude every board's directory, then add back only the
# selected BOARD_DIR — so unselected boards never get compiled in.
ARCH_PRUNE_BOARDS = $(foreach b,$(BOARDS),-not -path '$(ARCH_DIR)/$(b)/*')

# kernel sources
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
CSRCS     := $(filter-out lib/zuzu/zmalloc.c,$(CSRCS))
CSRCS     += $(LIBFDT_SRCS)
ASRCS_ALL = $(shell find $(NONARCH_DIRS) -name '*.S') \
            $(shell find $(ARCH_DIR) -name '*.S' $(ARCH_PRUNE_BOARDS)) \
            $(shell find $(BOARD_DIR) -name '*.S')
ASRCS     = $(filter-out $(ARCH_DIR)/crt0.S $(ARCH_DIR)/initrd.S,$(ASRCS_ALL))
OBJS      = $(CSRCS:%.c=build/%.o) $(ASRCS:%.S=build/%.o)
DEPS      = $(OBJS:.o=.d)
USER_DEPS = $(USER_CRT0:.o=.d) $(USER_APP_OBJS:.o=.d) $(ZCRT_OBJS:.o=.d) $(ULIB_OBJS:.o=.d)

TARGET = build/zuzu.elf

# sd card image configuration
SD_IMG         ?= build/sd.img
SD_IMG_SIZE_MB ?= 64
SD_VOL_LABEL   ?= ZUZU
SD_STAGE_DIR   ?= ZUZUSD

# default
all: $(TARGET) 

.SECONDARY: $(USER_APP_OBJS) $(ZCRT_OBJS) $(ULIB_OBJS)

# Objects bake in per-board flags (BOARD_LAYOUT_H, CPUFLAGS), so a BOARD
# switch must rebuild everything. The stamp file changes name with the board;
# every object depends on it, forcing a full rebuild when it (re)appears.
BOARD_STAMP = build/.board-$(BOARD)

$(BOARD_STAMP):
	@mkdir -p build
	@rm -f build/.board-*
	@touch $@

# compilation rules
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

build/user/lib/%.o: user/lib/%.c $(BOARD_STAMP)
	@mkdir -p $(dir $@)
	@echo "  CC      $<"
	@$(USER_CC) $(USER_CFLAGS) -Iuser/lib -c $< -o $@

$(USER_CRT0): $(ARCH_DIR)/crt0.S
	@mkdir -p $(dir $@)
	@echo "  AS      $<"
	@$(USER_CC) $(USER_CFLAGS) -x assembler-with-cpp -c $< -o $@

# user programs
define LINK_USER_PROG
build/user/$(1).elf: $$(USER_$(1)_OBJS) $(USER_CRT0) $(ZCRT_OBJS) $(ULIB_OBJS) user/user.ld
	@mkdir -p $$(dir $$@)
	@echo "  LD      $$@"
	@$(USER_LD) $(USER_LDFLAGS) $(USER_CRT0) $$(USER_$(1)_OBJS) $(ZCRT_OBJS) $(ULIB_OBJS) $(USER_LIBGCC) -o $$@
endef

$(foreach p,$(USER_PROGS),$(eval $(call LINK_USER_PROG,$(p))))

define STRIP_USER_PROG
build/user/$(1).stripped.elf: build/user/$(1).elf
	@echo "  STRIP   $$@"
	@$(USER_OBJCOPY) --strip-debug $$< $$@
endef

$(foreach p,$(USER_PROGS),$(eval $(call STRIP_USER_PROG,$(p))))

# initrd
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

# SD card workflow
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
#
#  sdimg-stage: copy DISK_PROGS ELFs into ZUZUSD/bin/
#  sdimg:       create sd.img from ZUZUSD/ (runs sdimg-stage first)
#  sdimg-clean: delete sd.img
#  sdimg-recreate: clean + rebuild

.PHONY: sdimg-stage sdimg-clean sdimg-recreate

sdimg-stage: $(DISK_PROG_PACKED_ELFS)
	@mkdir -p $(SD_STAGE_DIR)/bin
	@for prog in $(DISK_PROGS); do \
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

# Run / debug targets
.PHONY: run run-pcap debug

PCAP_FILE ?= /tmp/zuzu.pcap

QEMU_MACHINE = $(QEMU_MACH_$(BOARD))
QEMU_CPU     = $(QEMU_CPU_$(BOARD))
QEMU_BIN     = $(if $(QEMU_BIN_$(BOARD)),$(QEMU_BIN_$(BOARD)),qemu-system-arm)
QEMU_MEM     = $(if $(QEMU_MEM_$(BOARD)),$(QEMU_MEM_$(BOARD)),64M)
# Board NIC flags; empty for boards whose NIC QEMU does not emulate.
QEMU_NET     = $(QEMU_NET_$(BOARD))
# What QEMU boots: the ELF by default, a raw image where the board needs it.
QEMU_KERNEL  = $(if $(QEMU_KERNEL_$(BOARD)),$(QEMU_KERNEL_$(BOARD)),$(TARGET))

run: $(QEMU_KERNEL) $(DTB_FILE)
	@echo "  QEMU    $(QEMU_KERNEL)"
	@$(QEMU_BIN) -M $(QEMU_MACHINE) -cpu $(QEMU_CPU) -m $(QEMU_MEM) \
	    -kernel $(QEMU_KERNEL) -dtb $(DTB_FILE) -nographic \
	    -drive file=$(SD_IMG),if=sd,format=raw \
	    $(QEMU_NET)

run-bridged: $(TARGET) $(DTB_FILE)
	@echo "  QEMU    $(TARGET) [bridged]"
	@sudo $(QEMU_BIN) -M $(QEMU_MACHINE) -cpu $(QEMU_CPU) -m $(QEMU_MEM) \
	    -kernel $(TARGET) -dtb $(DTB_FILE) -nographic \
	    -drive file=$(SD_IMG),if=sd,format=raw \
	    -nic vmnet-bridged,model=lan9118,ifname=en0,mac=52:54:00:ab:cd:ef

run-pcap: $(TARGET) $(DTB_FILE)
	@echo "  QEMU    $(TARGET) [pcap -> $(PCAP_FILE)]"
	@$(QEMU_BIN) -M $(QEMU_MACHINE) -cpu $(QEMU_CPU) -m $(QEMU_MEM) \
	    -kernel $(TARGET) -dtb $(DTB_FILE) -nographic \
	    -drive file=$(SD_IMG),if=sd,format=raw \
	    -net nic,model=lan9118 -net user,id=n0 \
	    -object filter-dump,id=f0,netdev=n0,file=$(PCAP_FILE)
	@echo "  PCAP    wrote $(PCAP_FILE) (read with: tcpdump -nr $(PCAP_FILE))"

debug: $(TARGET) $(DTB_FILE)
	@echo "  QEMU    $(TARGET) (debug)"
	@$(QEMU_BIN) -M $(QEMU_MACHINE) -cpu $(QEMU_CPU) -m $(QEMU_MEM) \
	    -kernel $(TARGET) -dtb $(DTB_FILE) -nographic \
	    -drive file=$(SD_IMG),if=sd,format=raw \
	    $(QEMU_NET) \
	    -S -gdb tcp::1234

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

img: $(IMG)

# Misc targets
.PHONY: all dump clean img

dump: $(TARGET)
	@echo "  OBJDUMP $@"
	@$(OBJDUMP) -D $(TARGET) > build/zuzu.dump

deploy: all sdimg-recreate run

clean:
	@rm -rf build
	@echo "  CLEAN   build"

-include $(DEPS) $(USER_DEPS)

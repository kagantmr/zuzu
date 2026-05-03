CROSS   = arm-none-eabi-
CC      = $(CROSS)gcc
LD      = $(CROSS)ld
OBJDUMP = $(CROSS)objdump

UNAME_S := $(shell uname -s)

OPTIMIZATION_LEVEL ?= 2
DEBUG_BUILD        ?= 1
DTB_DEBUG_WALK     ?= 0
EARLY_UART         ?= 0
DEBUG_PRINT        ?= 1
BANNER             ?= 1
FANCY_PANIC        ?= 1


BOARD_DIR     = arch/arm/vexpress-a15
LINKER_SCRIPT = $(BOARD_DIR)/linker.ld
DTB_FILE      = arch/arm/dtb/vexpress-a15/vexpress-v2p-ca15-tc1.dtb
MAP           = build/zuzu.map

CPUFLAGS = -mcpu=cortex-a15
CFLAGS   = -ffreestanding -O$(OPTIMIZATION_LEVEL) -fno-omit-frame-pointer \
           -Wall -Wextra -Werror $(CPUFLAGS) -I. -Iinclude -MMD -MP -g \
           -D__KERNEL__
LDFLAGS  = -T $(LINKER_SCRIPT) -Map=$(MAP)

ifeq ($(BANNER), 0)
    CFLAGS += -DZUZU_BANNER_DISABLE
endif
ifeq ($(FANCY_PANIC), 1)
    CFLAGS += -DPANIC_FULL_SCREEN
endif
ifeq ($(DEBUG_PRINT), 1)
    CFLAGS += -DDEBUG_PRINT
endif
ifeq ($(DEBUG_BUILD), 1)
    CFLAGS += -UNDEBUG -DDEBUG -DZUZU_BANNER_SHOW_ADDR
else
    CFLAGS += -DNDEBUG -UDEBUG -UZUZU_BANNER_SHOW_ADDR
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
USER_LD      = $(CROSS)ld
USER_OBJCOPY = $(CROSS)objcopy
KERNEL_LIBGCC = $(shell $(CC) $(CPUFLAGS) -print-libgcc-file-name)
USER_LIBGCC   = $(shell $(USER_CC) $(CPUFLAGS) -mfloat-abi=soft -print-libgcc-file-name)

USER_CFLAGS  = -ffreestanding -nostdlib -O$(OPTIMIZATION_LEVEL) -Wall -Wextra \
               $(CPUFLAGS) -Iuser/include -Iinclude -MMD -MP -g -mfloat-abi=hard -mfpu=vfpv4
USER_LDFLAGS = -T user/user.ld

ifeq ($(DEBUG_BUILD), 1)
    USER_CFLAGS += -UNDEBUG -DDEBUG
else
    USER_CFLAGS += -DNDEBUG -UDEBUG
endif

BOOT_PROGS = sysd devmgr zuart zusd fat32d fbox zzsh
DISK_PROGS = test

USER_PROGS = $(BOOT_PROGS) $(DISK_PROGS)

# zcrt + user lib sources
ZCRT_SRCS = $(wildcard lib/zcrt/*.c)
ZCRT_OBJS = $(patsubst lib/zcrt/%.c,build/user/zcrt/%.o,$(ZCRT_SRCS))

ULIB_SRCS = $(filter-out user/lib/service.c,$(wildcard user/lib/*.c))
ULIB_OBJS = $(patsubst user/%.c,build/user/%.o,$(ULIB_SRCS))

SERVICE_SRCS = user/lib/service.c
SERVICE_OBJS = $(patsubst user/%.c,build/user/%.o,$(SERVICE_SRCS))

# derived user program sources + objects
USER_CRT0      = build/user/crt0.o
BOOT_PROG_ELFS = $(foreach p,$(BOOT_PROGS),build/user/$(p).elf)
DISK_PROG_ELFS = $(foreach p,$(DISK_PROGS),build/user/$(p).elf)
USER_ELFS      = $(BOOT_PROG_ELFS) $(DISK_PROG_ELFS)
INITRD         = build/initrd.cpio
INITRD_EXTRA_DIR ?= initrd
INITRD_EXTRA_FILES := $(shell find $(INITRD_EXTRA_DIR) -type f 2>/dev/null)

$(foreach p,$(USER_PROGS),$(eval USER_$(p)_SRCS := $(wildcard user/$(p)/*.c)))
$(foreach p,$(USER_PROGS),$(eval USER_$(p)_OBJS := $(patsubst user/%.c,build/user/%.o,$(USER_$(p)_SRCS))))
USER_APP_OBJS = $(foreach p,$(USER_PROGS),$(USER_$(p)_OBJS))

SRC_DIRS = arch core drivers kernel lib/zcrt

# kernel sources
CSRCS     = $(shell find $(SRC_DIRS) -name '*.c')
CSRCS     := $(filter-out lib/zcrt/zmalloc.c,$(CSRCS))
ASRCS_ALL = $(shell find $(SRC_DIRS) -name '*.S')
ASRCS     = $(filter-out arch/arm/crt0.S arch/arm/initrd.S,$(ASRCS_ALL))
OBJS      = $(CSRCS:%.c=build/%.o) $(ASRCS:%.S=build/%.o)
DEPS      = $(OBJS:.o=.d)
USER_DEPS = $(USER_CRT0:.o=.d) $(USER_APP_OBJS:.o=.d) $(ZCRT_OBJS:.o=.d) $(ULIB_OBJS:.o=.d) $(SERVICE_OBJS:.o=.d)

TARGET = build/zuzu.elf

# sd card image configuration
SD_IMG         ?= build/sd.img
SD_IMG_SIZE_MB ?= 64
SD_VOL_LABEL   ?= ZUZU
SD_STAGE_DIR   ?= ZUZUSD

# default
all: $(TARGET)

.SECONDARY: $(USER_APP_OBJS) $(ZCRT_OBJS) $(ULIB_OBJS) $(SERVICE_OBJS)

# compilation rules
build/user/%.o: user/%.c
	@mkdir -p $(dir $@)
	$(USER_CC) $(USER_CFLAGS) -c $< -o $@

build/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

build/%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -x assembler-with-cpp -c $< -o $@

build/user/zcrt/%.o: lib/zcrt/%.c
	@mkdir -p $(dir $@)
	$(USER_CC) $(USER_CFLAGS) -c $< -o $@

build/user/lib/%.o: user/lib/%.c
	@mkdir -p $(dir $@)
	$(USER_CC) $(USER_CFLAGS) -Iuser/lib -c $< -o $@

build/user/include/%.o: user/include/%.c
	@mkdir -p $(dir $@)
	$(USER_CC) $(USER_CFLAGS) -c $< -o $@

$(USER_CRT0): arch/arm/crt0.S
	@mkdir -p $(dir $@)
	$(USER_CC) $(USER_CFLAGS) -x assembler-with-cpp -c $< -o $@

# user programs
define LINK_USER_PROG
build/user/$(1).elf: $$(USER_$(1)_OBJS) $(USER_CRT0) $(ZCRT_OBJS) $(ULIB_OBJS) $$(if $$(filter fbox fat32d zzsh,$(1)),$(SERVICE_OBJS)) user/user.ld
	@mkdir -p $$(dir $$@)
	$(USER_LD) $(USER_LDFLAGS) $(USER_CRT0) $$(USER_$(1)_OBJS) $(ZCRT_OBJS) $(ULIB_OBJS) $$(if $$(filter fbox fat32d zzsh,$(1)),$(SERVICE_OBJS)) $(USER_LIBGCC) -o $$@
endef

$(foreach p,$(USER_PROGS),$(eval $(call LINK_USER_PROG,$(p))))

# initrd
$(INITRD): $(BOOT_PROG_ELFS) $(INITRD_EXTRA_FILES)
	@rm -rf build/initrd
	@mkdir -p build/initrd/bin
	@for prog in $(BOOT_PROGS); do \
		cp build/user/$$prog.elf build/initrd/bin/$$prog; \
	done
	@if [ -d "$(INITRD_EXTRA_DIR)" ]; then \
		cp -R $(INITRD_EXTRA_DIR)/. build/initrd/; \
	fi
	cd build/initrd && find . -not -name '.' | sort | cpio -o -H newc > ../initrd.cpio 2>/dev/null
	@echo "  CPIO    $@ ($(words $(BOOT_PROGS)) boot program(s))"

build/arch/arm/initrd.o: arch/arm/initrd.S $(INITRD)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -x assembler-with-cpp -c $< -o $@

# Kernel link
$(TARGET): $(OBJS) build/arch/arm/initrd.o $(LINKER_SCRIPT)
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) $(OBJS) build/arch/arm/initrd.o $(KERNEL_LIBGCC) -o $@

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

sdimg-stage: $(DISK_PROG_ELFS)
	@mkdir -p $(SD_STAGE_DIR)/bin
	@for prog in $(DISK_PROGS); do \
		cp build/user/$$prog.elf $(SD_STAGE_DIR)/bin/$$prog; \
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
.PHONY: run debug

run: $(TARGET)
	qemu-system-arm -M vexpress-a15 -cpu cortex-a15 -m 64M \
	    -kernel $(TARGET) -dtb $(DTB_FILE) -nographic \
	    -drive file=$(SD_IMG),if=sd,format=raw

debug: $(TARGET)
	qemu-system-arm -M vexpress-a15 -cpu cortex-a15 -m 64M \
	    -kernel $(TARGET) -dtb $(DTB_FILE) -nographic \
	    -S -gdb tcp::1234

# Misc targets
.PHONY: all dump clean

dump: $(TARGET)
	$(OBJDUMP) -D $(TARGET) > build/zuzu.dump

deploy: all sdimg-recreate run

clean:
	rm -rf build

-include $(DEPS) $(USER_DEPS)
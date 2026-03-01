CROSS   = arm-none-eabi-
CC      = $(CROSS)gcc
LD      = $(CROSS)ld
OBJDUMP = $(CROSS)objdump

# Build options
OPTIMIZATION_LEVEL ?= 2
DEBUG_BUILD ?= 1
DTB_DEBUG_WALK ?= 0
EARLY_UART ?= 0
DEBUG_PRINT ?= 1
SP804_TIMER ?= 0
BANNER ?= 1
STATS_MODE ?= 1
FANCY_PANIC ?= 1


# Board configuration variables
BOARD_DIR     = arch/arm/vexpress-a15
LINKER_SCRIPT = $(BOARD_DIR)/linker.ld
DTB_FILE      = arch/arm/dtb/vexpress-a15/vexpress-v2p-ca15-tc1.dtb
MAP = build/zuzu.map

CPUFLAGS = -mcpu=cortex-a15
CFLAGS   = -ffreestanding -O$(OPTIMIZATION_LEVEL) -fno-omit-frame-pointer -Wall -Wextra -Werror $(CPUFLAGS) -I. -MMD -MP -g
LDFLAGS  = -T $(LINKER_SCRIPT) -Map=$(MAP)

# Userspace build variables
USER_CC      = $(CROSS)gcc
USER_LD      = $(CROSS)ld
USER_OBJCOPY = $(CROSS)objcopy

USER_CFLAGS  = -ffreestanding -nostdlib -O$(OPTIMIZATION_LEVEL) -Wall -Wextra \
               $(CPUFLAGS) -I user/include -MMD -MP -g
USER_LDFLAGS = -T user/user.ld

# List every user program directory here (under user/)
USER_PROGS   = init hello zuart

# Derived paths
USER_CRT0    = build/user/crt0.o
USER_ELFS    = $(foreach p,$(USER_PROGS),build/user/$(p)/$(p).elf)
INITRD       = build/initrd.cpio


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
    # Enable assertions (default C standard for enabling assertions)
    # might also add -DDEBUG_LOGGING for KINFO/KWARN
    CFLAGS += -UNDEBUG -DDEBUG -DZUZU_BANNER_SHOW_ADDR
else
    # Disable assertions for release builds
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

ifeq ($(SP804_TIMER), 1)
    CFLAGS += -DSP804_TIMER
else
    CFLAGS += -USP804_TIMER
endif

ifeq ($(STATS_MODE), 1)
    CFLAGS += -DSTATS_MODE
else
    CFLAGS += -USTATS_MODE
endif


# Directories to search for source files
SRC_DIRS = arch core drivers kernel lib

# Find sources
CSRCS    = $(shell find $(SRC_DIRS) -name '*.c')
ASRCS    = $(shell find $(SRC_DIRS) -name '*.s')
SRCS     = $(CSRCS) $(ASRCS)

# Object files in build/
OBJS     = $(CSRCS:%.c=build/%.o) \
           $(ASRCS:%.s=build/%.o)

DEPS     = $(OBJS:.o=.d)

TARGET   = build/zuzu.elf 

all: $(TARGET)

# Compile C sources
build/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Compile assembly sources
build/%.o: %.s
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -x assembler-with-cpp -c $< -o $@

# Userspace build rules

# CRT0 is the shared entry stub for all user programs
$(USER_CRT0): user/crt0.s
	@mkdir -p $(dir $@)
	$(USER_CC) $(USER_CFLAGS) -x assembler-with-cpp -c $< -o $@

# compile .c sources and link with crt0
build/user/init/init.elf: user/init/main.c $(USER_CRT0) user/user.ld
	@mkdir -p $(dir $@)
	$(USER_CC) $(USER_CFLAGS) -c user/init/main.c -o build/user/init/main.o
	$(USER_LD) $(USER_LDFLAGS) $(USER_CRT0) build/user/init/main.o -o $@

build/user/hello/hello.elf: user/hello/main.c $(USER_CRT0) user/user.ld
	@mkdir -p $(dir $@)
	$(USER_CC) $(USER_CFLAGS) -c user/hello/main.c -o build/user/hello/main.o
	$(USER_LD) $(USER_LDFLAGS) $(USER_CRT0) build/user/hello/main.o -o $@

build/user/zuart/zuart.elf: user/zuart/main.c $(USER_CRT0) user/user.ld
	@mkdir -p $(dir $@)
	$(USER_CC) $(USER_CFLAGS) -c user/zuart/main.c -o build/user/zuart/main.o
	$(USER_LD) $(USER_LDFLAGS) $(USER_CRT0) build/user/zuart/main.o -o $@

# Pack all user ELFs into a CPIO newc archive.
# The directory tree inside the archive becomes the namespace that
# initrd_find() searches, e.g. "bin/init".
$(INITRD): $(USER_ELFS)
	@rm -rf build/initrd
	@mkdir -p build/initrd/bin
	cp build/user/init/init.elf build/initrd/bin/init
	cp build/user/hello/hello.elf build/initrd/bin/hello
	cp build/user/zuart/zuart.elf build/initrd/bin/zuart
	cd build/initrd && find . -not -name '.' | sort | cpio -o -H newc > ../initrd.cpio 2>/dev/null
	@echo "  CPIO    $@ ($(words $(USER_PROGS)) program(s))"

build/arch/arm/initrd.o: arch/arm/initrd.s $(INITRD)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -x assembler-with-cpp -c $< -o $@

# Kernel link

# Link
$(TARGET): $(OBJS) $(LINKER_SCRIPT)
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) $(OBJS) -o $@

# Run in QEMU
run: $(TARGET)
	qemu-system-arm -M vexpress-a15 -cpu cortex-a15 -m 64M -kernel $(TARGET) -dtb $(DTB_FILE) -nographic

debug: $(TARGET)
	qemu-system-arm -M vexpress-a15 -cpu cortex-a15 -kernel $(TARGET) -dtb $(DTB_FILE) -nographic -S -s

size:
	arm-none-eabi-size $(TARGET)

# Disassemble
dump: $(TARGET)
	$(OBJDUMP) -d $(TARGET) > build/zuzu.asm

clean:
	rm -rf build

-include $(DEPS)
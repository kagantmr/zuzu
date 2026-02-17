CROSS   = arm-none-eabi-
CC      = $(CROSS)gcc
LD      = $(CROSS)ld
OBJDUMP = $(CROSS)objdump

# Board configuration variables
BOARD_DIR     = arch/arm/vexpress-a15
LINKER_SCRIPT = $(BOARD_DIR)/linker.ld
DTB_FILE      = arch/arm/dtb/vexpress-a15/vexpress-v2p-ca15-tc1.dtb
MAP = build/zuzu.map


CPUFLAGS = -mcpu=cortex-a15
CFLAGS   = -ffreestanding -O2 -fno-omit-frame-pointer -Wall -Wextra -Werror $(CPUFLAGS) -I. -MMD -MP -g
LDFLAGS  = -T $(LINKER_SCRIPT) -Map=$(MAP)

# Debug options
DEBUG_BUILD ?= 1
DTB_DEBUG_WALK ?= 0
EARLY_UART ?= 0
SP804_TIMER ?= 0
BANNER ?= 1
STATS_MODE ?= 1


ifeq ($(BANNER), 0)
    CFLAGS += -DZUZU_BANNER_DISABLE
endif
ifeq ($(DEBUG_BUILD), 1)
    # Enable assertions (default C standard for enabling assertions)
    # might also add -DDEBUG_LOGGING for KINFO/KWARN
    CFLAGS += -UNDEBUG -DDEBUG -DDEBUG_PRINT -DZUZU_BANNER_SHOW_ADDR
else
    # Disable assertions for release builds
    CFLAGS += -DNDEBUG -UDEBUG -UDEBUG_PRINT -UZUZU_BANNER_SHOW_ADDR
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
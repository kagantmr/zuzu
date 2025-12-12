CROSS   = arm-none-eabi-
CC      = $(CROSS)gcc
LD      = $(CROSS)ld
OBJDUMP = $(CROSS)objdump

# Board configuration variables
BOARD_DIR     = arch/arm/vexpress-a15
LINKER_SCRIPT = $(BOARD_DIR)/linker.ld
DTB_FILE      = arch/arm/dtb/vexpress-a15/vexpress-v2p-ca15-tc1.dtb



CPUFLAGS = -mcpu=cortex-a15
# -I. is crucial: It lets you include files relative to the project root
CFLAGS   = -ffreestanding -Wall -Wextra -Werror $(CPUFLAGS) -I. -MMD -MP -g
LDFLAGS  = -T $(LINKER_SCRIPT)

# Set DEBUG_BUILD to 1 to enable assertions and extra logging
DEBUG_BUILD ?= 1

ifeq ($(DEBUG_BUILD), 1)
    # Enable assertions (default C standard for enabling assertions)
    # You might also add -DDEBUG_LOGGING for KINFO/KWARN
    CFLAGS += -UNDEBUG -DDEBUG
else
    # Disable assertions for release builds
    CFLAGS += -DNDEBUG
endif

# Directories to search for source files
SRC_DIRS = arch core drivers kernel lib

# Find sources
CSRCS    = $(shell find $(SRC_DIRS) -name '*.c')
ASRCS    = $(shell find $(SRC_DIRS) -name '*.s')
SRCS     = $(CSRCS) $(ASRCS)

# Object files in build/
# Example mapping: core/kprintf.c -> build/core/kprintf.o
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

# Disassemble
dump: $(TARGET)
	$(OBJDUMP) -d $(TARGET) > build/zuzu.asm

clean:
	rm -rf build

-include $(DEPS)
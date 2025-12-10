$(info INCLUDE_DIRS = $(INCLUDE_DIRS))
$(info CFLAGS = $(CFLAGS))

CROSS   = arm-none-eabi-
CC      = $(CROSS)gcc
LD      = $(CROSS)ld
OBJDUMP = $(CROSS)objdump

ARCH     := arm
BOARD    := vexpress-a15

CPUFLAGS = -mcpu=cortex-a15
CFLAGS   = -ffreestanding -Wall -Wextra -Werror $(CPUFLAGS) -g -MMD -MP

# Automatically collect all include directories
INCLUDE_DIRS := $(shell find arch core kernel lib drivers -type d)
CFLAGS += $(addprefix -I, $(INCLUDE_DIRS))

LDSCRIPT = arch/$(ARCH)/$(BOARD)/linker.ld
LDFLAGS  = -T $(LDSCRIPT)

# Find sources
CSRCS = $(shell find arch core kernel lib drivers -name '*.c')
ASRCS = $(shell find arch core kernel lib drivers -name '*.s')
SRCS  = $(CSRCS) $(ASRCS)

# Object files in build/
OBJS = $(patsubst %.c, build/%.o, $(CSRCS)) \
       $(patsubst %.s, build/%.o, $(ASRCS))

DEPS = $(OBJS:.o=.d)

TARGET = build/zuzu.elf

all: $(TARGET)

# Compilation rules
build/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

build/%.o: %.s
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -x assembler-with-cpp -c $< -o $@

# Link
$(TARGET): $(OBJS) $(LDSCRIPT)
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) $(OBJS) -o $@

# Run in QEMU
run: $(TARGET)
	qemu-system-arm -M vexpress-a15 -cpu cortex-a15 \
		-kernel $(TARGET) \
		-dtb arch/arm/dtb/vexpress-a15/vexpress-v2p-ca15-tc1.dtb \
		-nographic

debug: $(TARGET)
	qemu-system-arm -M vexpress-a15 -cpu cortex-a15 \
		-kernel $(TARGET) \
		-dtb arch/arm/dtb/vexpress-a15/vexpress-v2p-ca15-tc1.dtb \
		-nographic -S -s

# Disassemble
dump: $(TARGET)
	$(OBJDUMP) -d $(TARGET) > build/zuzu.asm

clean:
	rm -rf build

-include $(DEPS)
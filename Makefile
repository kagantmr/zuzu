CROSS   = arm-none-eabi-
CC      = $(CROSS)gcc
LD      = $(CROSS)ld
OBJDUMP = $(CROSS)objdump

CPUFLAGS = -mcpu=cortex-a15
CFLAGS   = -ffreestanding -Wall -Wextra -Werror $(CPUFLAGS) -Iinclude -MMD -MP
LDFLAGS  = -T src/arch/arm/linker.ld

# Find sources
CSRCS    = $(shell find src -name '*.c')
ASRCS    = $(shell find src -name '*.s')
SRCS     = $(CSRCS) $(ASRCS)

# Object files in build/
OBJS     = $(patsubst src/%.c, build/%.o, $(CSRCS)) \
           $(patsubst src/%.s, build/%.o, $(ASRCS))

DEPS     = $(OBJS:.o=.d)

TARGET   = build/zuzu.elf

all: $(TARGET)

# Compile C sources
build/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Compile assembly sources
build/%.o: src/%.s
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -x assembler-with-cpp -c $< -o $@

# Link
$(TARGET): $(OBJS)
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) $(OBJS) -o $@

# Run in QEMU
run: $(TARGET)
	qemu-system-arm -M vexpress-a15 -cpu cortex-a15 -kernel $(TARGET) -nographic

# Disassemble
dump: $(TARGET)
	$(OBJDUMP) -d $(TARGET) > build/zuzu.asm

clean:
	rm -rf build

-include $(DEPS)
CROSS   = arm-none-eabi-
CC      = $(CROSS)gcc
LD      = $(CROSS)ld
OBJDUMP = $(CROSS)objdump

CPUFLAGS = -mcpu=cortex-a15
CFLAGS   = -ffreestanding -Wall -Wextra -Werror $(CPUFLAGS) -Iinclude -MMD -MP
LDFLAGS  = -T linker.ld

CSRCS    = $(shell find src -name '*.c')
SRSCS    = $(shell find src -name '*.s')
SRCS     = $(CSRCS) $(SRSCS)
OBJS     = $(patsubst src/%.c,build/%.o,$(CSRCS)) $(patsubst src/%.s,build/%.o,$(SRSCS))
DEPS     = $(OBJS:.o=.d)

TARGET   = build/zuzuMicrokernel.elf

all: $(TARGET)

# Compile C
build/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Compile assembly
build/%.o: src/%.s
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -x assembler-with-cpp -c $< -o $@

# Link everything
$(TARGET): $(OBJS)
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) $(OBJS) -o $@

# Run in QEMU
run: $(TARGET)
	qemu-system-arm -M vexpress-a15 -cpu cortex-a15 -kernel $(TARGET) -nographic

# Disassemble
dump: $(TARGET)
	$(OBJDUMP) -d $(TARGET) > build/zuzuMicrokernel.asm

clean:
	rm -rf build

# Include dependency files
-include $(DEPS)
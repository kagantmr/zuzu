CROSS   = arm-none-eabi-
CC      = $(CROSS)gcc
LD      = $(CROSS)ld
OBJDUMP = $(CROSS)objdump

CPUFLAGS = -mcpu=cortex-a15
CFLAGS   = -ffreestanding -Wall -Wextra -Werror $(CPUFLAGS)
LDFLAGS  = -T linker.ld

TARGET   = build/kernel.elf
OBJS     = build/_start.o build/start.o

all: $(TARGET)

# Compile assembly and C source from src/boot/
build/%.o: src/boot/%.s
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

build/%.o: src/boot/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Link everything
$(TARGET): $(OBJS)
	$(LD) $(LDFLAGS) $(OBJS) -o $@

# Run in QEMU
run: $(TARGET)
	qemu-system-arm -M vexpress-a15 -cpu cortex-a15 -kernel $(TARGET) -nographic

# Disassemble
dump: $(TARGET)
	$(OBJDUMP) -d $(TARGET) > build/kernel.asm

clean:
	rm -rf build
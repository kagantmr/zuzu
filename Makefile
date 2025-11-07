CROSS   = arm-none-eabi-
CC      = $(CROSS)gcc
LD      = $(CROSS)ld
OBJDUMP = $(CROSS)objdump
OBJCOPY = $(CROSS)objcopy

CPUFLAGS = -march=armv7-a -mcpu=cortex-a15
CFLAGS   = -ffreestanding -Wall -Wextra -Werror $(CPUFLAGS)
LDFLAGS  = -T linker.ld

TARGET = kernel.elf
OBJS   = _start.o start.o

all: $(TARGET)

# Compile both assembly and C with GCC
%.o: %.s
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Link everything
$(TARGET): $(OBJS)
	$(LD) $(LDFLAGS) $(OBJS) -o $@

# Run in QEMU
run: $(TARGET)
	qemu-system-arm -M vexpress-a15 -cpu cortex-a15 -kernel $(TARGET) -nographic

# Optional disassembly for debugging
dump: $(TARGET)
	$(OBJDUMP) -d $(TARGET) > kernel.asm

clean:
	rm -f $(OBJS) $(TARGET) kernel.asm
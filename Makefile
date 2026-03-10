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
FANCY_PANIC ?= 1

# Board configuration
BOARD_DIR     = arch/arm/vexpress-a15
LINKER_SCRIPT = $(BOARD_DIR)/linker.ld
DTB_FILE      = arch/arm/dtb/vexpress-a15/vexpress-v2p-ca15-tc1.dtb
MAP           = build/zuzu.map

CPUFLAGS = -mcpu=cortex-a15
CFLAGS = -ffreestanding -O$(OPTIMIZATION_LEVEL) -fno-omit-frame-pointer \
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

# Userspace variables
USER_CC      = $(CROSS)gcc
USER_LD      = $(CROSS)ld
USER_OBJCOPY = $(CROSS)objcopy

USER_CFLAGS  = -ffreestanding -nostdlib -O$(OPTIMIZATION_LEVEL) -Wall -Wextra \
               $(CPUFLAGS) -I user/include -Iinclude -MMD -MP -g -mfloat-abi=soft
USER_LDFLAGS = -T user/user.ld

# List every user program directory here
USER_PROGS   = init zuart nametable shmem_test zzsh

# ZCRT library objects (compiled from include/zcrt/)
ZCRT_SRCS = $(wildcard include/zcrt/*.c)
ZCRT_OBJS = $(patsubst include/zcrt/%.c,build/user/zcrt/%.o,$(ZCRT_SRCS))

# Derived paths
USER_CRT0    = build/user/crt0.o
USER_ELFS    = $(foreach p,$(USER_PROGS),build/user/$(p).elf)
INITRD       = build/initrd.cpio

# --- Flags Logic ---
ifeq ($(DEBUG_BUILD), 1)
    CFLAGS      += -UNDEBUG -DDEBUG -DZUZU_BANNER_SHOW_ADDR
    USER_CFLAGS += -UNDEBUG -DDEBUG
else
    CFLAGS      += -DNDEBUG -UDEBUG -UZUZU_BANNER_SHOW_ADDR
    USER_CFLAGS += -DNDEBUG -UDEBUG
endif
# (Add other flag checks here as per your snippet...)

SRC_DIRS = arch core drivers kernel include/zcrt

# Find sources
CSRCS    = $(shell find $(SRC_DIRS) -name '*.c')
ASRCS_ALL = $(shell find $(SRC_DIRS) -name '*.S')

# Filter out initrd.S to prevent the "multiple definition of _initrd_start" error
ASRCS     = $(filter-out arch/arm/crt0.S arch/arm/initrd.S,$(ASRCS_ALL))
OBJS      = $(CSRCS:%.c=build/%.o) $(ASRCS:%.S=build/%.o)
DEPS      = $(OBJS:.o=.d)

TARGET   = build/zuzu.elf 

all: $(TARGET)

# Kernel Compilation
build/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

build/%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -x assembler-with-cpp -c $< -o $@

# --- ZCRT Compilation ---
build/user/zcrt/%.o: include/zcrt/%.c
	@mkdir -p $(dir $@)
	$(USER_CC) $(USER_CFLAGS) -c $< -o $@

# --- Userspace Build Rules ---

$(USER_CRT0): arch/arm/crt0.S
	@mkdir -p $(dir $@)
	$(USER_CC) $(USER_CFLAGS) -x assembler-with-cpp -c $< -o $@

# Links User Main + CRT0 + ZCRT Library
build/user/%.elf: user/%/main.c $(USER_CRT0) $(ZCRT_OBJS) user/user.ld
	@mkdir -p build/user
	$(USER_CC) $(USER_CFLAGS) -c $< -o build/user/$*.o
	$(USER_LD) $(USER_LDFLAGS) $(USER_CRT0) build/user/$*.o $(ZCRT_OBJS) -o $@

$(INITRD): $(USER_ELFS)
	@rm -rf build/initrd
	@mkdir -p build/initrd/bin
	@for prog in $(USER_PROGS); do \
		cp build/user/$$prog.elf build/initrd/bin/$$prog; \
	done
	cd build/initrd && find . -not -name '.' | sort | cpio -o -H newc > ../initrd.cpio 2>/dev/null
	@echo "  CPIO    $@ ($(words $(USER_PROGS)) program(s))"

build/arch/arm/initrd.o: arch/arm/initrd.S $(INITRD)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -x assembler-with-cpp -c $< -o $@

# Kernel Link
$(TARGET): $(OBJS) build/arch/arm/initrd.o $(LINKER_SCRIPT)
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) $(OBJS) build/arch/arm/initrd.o -o $@

# Helpers
run: $(TARGET)
	qemu-system-arm -M vexpress-a15 -cpu cortex-a15 -m 64M -kernel $(TARGET) -dtb $(DTB_FILE) -nographic

clean:
	rm -rf build

-include $(DEPS)
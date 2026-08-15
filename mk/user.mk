# mk/user.mk - user-space programs: tier-1 (zcrt) and tier-2 (newlib).
#
# Requires: config.mk, toolchain.mk.

# ---- user (tier-1, zuzu libc) flags ----------------------------------------
USER_LIBGCC = $(shell $(USER_CC) $(CPUFLAGS) $(ARCH_USER_FP) -print-libgcc-file-name)

USER_CFLAGS  = -ffreestanding -nostdlib -O$(USER_OPTIMIZATION_LEVEL) -Wall -Wextra -Wshadow \
               $(CPUFLAGS) $(INCLUDES) -MMD -MP -g $(ARCH_USER_FP) \
               -DBOARD_LAYOUT_H='"$(BOARD_LAYOUT_H)"' -DLOG_LEVEL=$(LOG_LEVEL)
USER_LDFLAGS = -nostdlib -Wl,-T,user/user.ld

ifeq ($(DEBUG_BUILD), 1)
    USER_CFLAGS += -DDEBUG
else
    USER_CFLAGS += -DNDEBUG
endif
ifneq ($(ZUZU_BENCH), 0)
    USER_CFLAGS += -DZUZU_BENCH
endif

# ---- tier-2 (newlib) flags --------------------------------------------------
# Newlib's headers must win over include/, but <zuzu/...> must still resolve.
# This dir exposes zuzu/ alone. (Goes away with the klib/ split.)
NEWLIB_INC = build/newlib-include

NEWLIB_USER_CFLAGS = -O$(USER_OPTIMIZATION_LEVEL) -Wall -Wextra -mthumb \
                     $(CPUFLAGS) -I. -I$(NEWLIB_INC) -Iarch/include -Iarch/$(ARCH)/include \
                     -MMD -MP -g $(ARCH_USER_FP) \
                     -DBOARD_LAYOUT_H='"$(BOARD_LAYOUT_H)"'

# C-only additions: -include force-includes C declarations, which breaks
# crt0-newlib.o's assembler compile (also built with NEWLIB_USER_CFLAGS,
# via -x assembler-with-cpp) if added above. Only used by the two C compile
# rules below.
NEWLIB_USER_CFLAGS_C = $(NEWLIB_USER_CFLAGS) \
                        -include include/newlib_compat.h -DSSIZE_MAX=0x7fffffff

NEWLIB_USER_LDFLAGS = -nostartfiles -Wl,-T,user/user.ld \
                      -mthumb -mcpu=cortex-a15 -mfloat-abi=hard -mfpu=vfpv4

# Fails only when a tier-2 recipe actually runs, so kernel/tier-1-only
# builds on a machine without a newlib toolchain are unaffected.
define check-newlib-toolchain
	@if [ -z "$(NEWLIB_CROSS)" ]; then \
		echo "  ERROR   no newlib-capable arm-none-eabi-gcc found for tier-2 build."; \
		echo "          install one (e.g. Arm GNU Toolchain) or set NEWLIB_CROSS=/path/to/arm-none-eabi-"; \
		exit 1; \
	fi
endef

# ---- user programs ----------------------------------------------------------
# Auto-discovered: every directory under a role dir is one program, named
# after its directory (sources inside may nest arbitrarily). ELF/initrd
# naming stays the bare program name regardless of where its sources live.
# Role decides packaging and libc tier:
#   BOOT_ROLES   -> initrd, tier-1
#   DISK_ROLES   -> SD card image, tier-1
#   NEWLIB_ROLES -> SD card image, tier-2 (newlib)
BOOT_ROLES   = services drivers shell
DISK_ROLES   = test_apps
NEWLIB_ROLES = newlib_apps
# Static .a libraries staged onto the SD card (headers/config alongside them
# go straight into ZUZUSD/ — see sdcard.mk's "SD card workflow" comment).
LIB_ROLES    = libs

prog_dirs = $(shell find $(foreach r,$(1),user/$(r)) -mindepth 1 -maxdepth 1 -type d 2>/dev/null)
BOOT_PROG_DIRS   := $(call prog_dirs,$(BOOT_ROLES))
DISK_PROG_DIRS   := $(call prog_dirs,$(DISK_ROLES))
NEWLIB_PROG_DIRS := $(call prog_dirs,$(NEWLIB_ROLES))
LIB_PROG_DIRS    := $(call prog_dirs,$(LIB_ROLES))

BOOT_PROGS   := $(notdir $(BOOT_PROG_DIRS))
DISK_PROGS   := $(notdir $(DISK_PROG_DIRS))
NEWLIB_PROGS := $(notdir $(NEWLIB_PROG_DIRS))
LIB_PROGS    := $(notdir $(LIB_PROG_DIRS))
USER_PROGS   := $(BOOT_PROGS) $(DISK_PROGS) $(NEWLIB_PROGS) $(LIB_PROGS)

$(foreach d,$(BOOT_PROG_DIRS) $(DISK_PROG_DIRS) $(NEWLIB_PROG_DIRS) $(LIB_PROG_DIRS),$(eval USER_DIR_$(notdir $(d)) := $(d)))
$(foreach p,$(USER_PROGS),$(eval USER_$(p)_SRCS := $(shell find $(USER_DIR_$(p)) -name '*.c')))
$(foreach p,$(USER_PROGS),$(eval USER_$(p)_OBJS := $(patsubst user/%.c,build/user/%.o,$(USER_$(p)_SRCS))))
USER_APP_OBJS := $(foreach p,$(BOOT_PROGS) $(DISK_PROGS) $(NEWLIB_PROGS),$(USER_$(p)_OBJS))
LIB_PROG_OBJS := $(foreach p,$(LIB_PROGS),$(USER_$(p)_OBJS))

# zcrt: the user-side runtime for tier-1 — klib rebuilt with user flags, the
# ZCRT libc (lib/), and the IPC runtime (lib/zuzu/).
ZCRT_SRCS := $(wildcard klib/*.c lib/*.c lib/zuzu/*.c)
ZCRT_OBJS := $(patsubst %.c,build/user/zcrt/%.o,$(ZCRT_SRCS))

ZCRT_ARCHIVE = build/user/libc.a
$(ZCRT_ARCHIVE): $(ZCRT_OBJS)
	@mkdir -p $(dir $@)
	@echo "  AR      $@"
	@rm -f $@
	@$(USER_AR) rcs $@ $(ZCRT_OBJS)

# Tier-2 zcrt allowlist: the IPC runtime plus zuzu's sbrk arena — and
# nothing else, so newlib owns every libc symbol (string/mem/stdio/...)
# unambiguously. sbrk.c must be here explicitly: the _sbrk stub calls
# sbrk(), and without this object the linker pulls newlib's sbrk() from
# libc.a, which calls _sbrk_r -> _sbrk -> sbrk: unbounded recursion that
# runs the stack into the guard page on the first malloc.
NEWLIB_ZCRT_SRCS := $(wildcard lib/zuzu/*.c) lib/sbrk.c
NEWLIB_ZCRT_OBJS := $(patsubst %.c,build/user/zcrt/%.o,$(NEWLIB_ZCRT_SRCS))
NEWLIB_STUB_SRCS := $(wildcard lib/posix/*.c)
NEWLIB_STUB_OBJS := $(patsubst lib/%.c,build/lib/%.o,$(NEWLIB_STUB_SRCS))

USER_CRT0             = build/user/crt0.o
NEWLIB_CRT0           = build/user/crt0-newlib.o
BOOT_PROG_PACKED_ELFS = $(BOOT_PROGS:%=build/user/%.stripped.elf)
# Everything staged into the SD image: tier-1 disk apps + tier-2 newlib apps.
SD_PROGS              = $(DISK_PROGS) $(NEWLIB_PROGS)
SD_PROG_PACKED_ELFS   = $(SD_PROGS:%=build/user/%.stripped.elf)
# Static libraries (user/libs/<name>/*.c) staged into ZUZUSD/lib/.
SD_LIBS               = $(LIB_PROGS)
SD_LIB_ARCHIVES       = $(SD_LIBS:%=build/user/lib/%.a)

# Every ELF that `all` needs to build so IntelliSense (Makefile Tools' dry
# run of the default target) sees compiler invocations for every tier.
ALL_USER_ELFS = $(BOOT_PROGS:%=build/user/%.elf) $(DISK_PROGS:%=build/user/%.elf) \
                $(NEWLIB_PROGS:%=build/user/%.elf)

USER_DEPS = $(USER_CRT0:.o=.d) $(NEWLIB_CRT0:.o=.d) $(USER_APP_OBJS:.o=.d) \
            $(LIB_PROG_OBJS:.o=.d) $(ZCRT_OBJS:.o=.d) $(NEWLIB_STUB_OBJS:.o=.d)

.SECONDARY: $(USER_APP_OBJS) $(LIB_PROG_OBJS) $(ZCRT_OBJS)

# ---- compilation rules ------------------------------------------------------
$(NEWLIB_INC)/zuzu:
	@mkdir -p $(NEWLIB_INC)
	@ln -sfn ../../include/zuzu $(NEWLIB_INC)/zuzu

# This newlib build's own <dirent.h> hard #errors on this target (no host
# directory backend), so shadow it with our own (see lib/posix/dirent.c).
$(NEWLIB_INC)/dirent.h:
	@mkdir -p $(NEWLIB_INC)
	@ln -sfn ../../include/dirent.h $(NEWLIB_INC)/dirent.h

NEWLIB_INC_STAMPS = $(NEWLIB_INC)/zuzu $(NEWLIB_INC)/dirent.h

# Must precede the generic build/user/%.o rule: make 3.81 picks the first
# matching pattern rule, not the most specific one.
build/user/newlib_apps/%.o: user/newlib_apps/%.c $(BOARD_STAMP) $(NEWLIB_INC_STAMPS)
	$(call check-newlib-toolchain)
	@mkdir -p $(dir $@)
	@echo "  CC[nl]  $<"
	@$(NEWLIB_CC) $(NEWLIB_USER_CFLAGS_C) -c $< -o $@

build/lib/posix/%.o: lib/posix/%.c $(BOARD_STAMP) $(NEWLIB_INC_STAMPS)
	$(call check-newlib-toolchain)
	@mkdir -p $(dir $@)
	@echo "  CC[nl]  $<"
	@$(NEWLIB_CC) $(NEWLIB_USER_CFLAGS_C) -c $< -o $@

build/user/%.o: user/%.c $(BOARD_STAMP)
	@mkdir -p $(dir $@)
	@echo "  CC      $<"
	@$(USER_CC) $(USER_CFLAGS) -c $< -o $@

build/user/zcrt/%.o: %.c $(BOARD_STAMP)
	@mkdir -p $(dir $@)
	@echo "  CC      $<"
	@$(USER_CC) $(USER_CFLAGS) -c $< -o $@

$(USER_CRT0): $(ARCH_DIR)/crt0.S
	@mkdir -p $(dir $@)
	@echo "  AS      $<"
	@$(USER_CC) $(USER_CFLAGS) -x assembler-with-cpp -c $< -o $@

$(NEWLIB_CRT0): $(ARCH_DIR)/crt0.S
	$(call check-newlib-toolchain)
	@mkdir -p $(dir $@)
	@echo "  AS[nl]  $<"
	@$(NEWLIB_CC) $(NEWLIB_USER_CFLAGS) -DZUZU_NEWLIB -x assembler-with-cpp -c $< -o $@

# ---- user program link rules ------------------------------------------------
define LINK_USER_PROG
build/user/$(1).elf: $$(USER_$(1)_OBJS) $(USER_CRT0) $(ZCRT_OBJS) user/user.ld
	@mkdir -p $$(dir $$@)
	@echo "  LD      $$@"
	@$(USER_LD) $(USER_LDFLAGS) $(USER_CRT0) $$(USER_$(1)_OBJS) $(ZCRT_OBJS) $(USER_LIBGCC) -o $$@
endef

$(foreach p,$(BOOT_PROGS) $(DISK_PROGS),$(eval $(call LINK_USER_PROG,$(p))))

define LINK_NEWLIB_PROG
build/user/$(1).elf: $$(USER_$(1)_OBJS) $(NEWLIB_CRT0) $(NEWLIB_STUB_OBJS) $(NEWLIB_ZCRT_OBJS) user/user.ld
	$$(call check-newlib-toolchain)
	@mkdir -p $$(dir $$@)
	@echo "  LD[nl]  $$@"
	@$(NEWLIB_LD) $(NEWLIB_USER_LDFLAGS) $(NEWLIB_CRT0) $$(USER_$(1)_OBJS) $(NEWLIB_STUB_OBJS) $(NEWLIB_ZCRT_OBJS) -o $$@
endef

$(foreach p,$(NEWLIB_PROGS),$(eval $(call LINK_NEWLIB_PROG,$(p))))

define LINK_USER_LIB
build/user/lib/$(1).a: $$(USER_$(1)_OBJS)
	@mkdir -p $$(dir $$@)
	@echo "  AR      $$@"
	@rm -f $$@
	@$(USER_AR) rcs $$@ $$(USER_$(1)_OBJS)
endef

$(foreach p,$(LIB_PROGS),$(eval $(call LINK_USER_LIB,$(p))))

build/user/%.stripped.elf: build/user/%.elf
	@echo "  STRIP   $@"
	@$(USER_OBJCOPY) --strip-debug $< $@

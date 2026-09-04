# mk/initrd.mk - initrd.cpio packaging.
#
# build/initrd.cpio is shipped to a bootloader/firmware as a standalone file
# (U-Boot's initrd.uImage, or a board's own firmware-loaded initramfs) — it
# is never linked into the kernel ELF.
#
# Requires: user.mk (ZXF_PROG_FILES).

INITRD             = build/initrd.cpio
INITRD_EXTRA_DIR  ?= initrd
INITRD_EXTRA_FILES := $(shell find $(INITRD_EXTRA_DIR) -type f 2>/dev/null)
# DISK_ROLES programs staged into the initrd too, in addition to the SD card
# image for boards (rpi4) that have no working SD driver yet. These stay
# plain ELF, unlike BOOT_ROLES programs (see ZXF_PROGS in user.mk).
INITRD_EXTRA_PROGS     =
INITRD_EXTRA_PROG_ELFS = $(INITRD_EXTRA_PROGS:%=build/user/%.stripped.elf)

$(INITRD): $(ZXF_PROG_FILES) $(INITRD_EXTRA_PROG_ELFS) $(INITRD_EXTRA_FILES)
	$(call check-tool,cpio,install it via your package manager (e.g. apt/brew install cpio).)
	@rm -rf build/initrd
	@mkdir -p build/initrd/bin
	@for prog in $(BOOT_PROGS) $(INITRD_EXTRA_PROGS); do \
		if echo "$(ZXF_PROGS)" | grep -qw "$$prog"; then \
			cp build/user/$$prog.zxf build/initrd/bin/$$prog; \
		else \
			cp build/user/$$prog.stripped.elf build/initrd/bin/$$prog; \
		fi; \
	done
	@if [ -d "$(INITRD_EXTRA_DIR)" ]; then \
		cp -R $(INITRD_EXTRA_DIR)/. build/initrd/; \
	fi
	@cd build/initrd && find . -not -name '.' | sort | cpio -o -H newc > ../initrd.cpio 2>/dev/null
	@echo "  CPIO    $@ ($(words $(BOOT_PROGS) $(INITRD_EXTRA_PROGS)) boot program(s))"

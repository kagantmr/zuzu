# mk/qemu.mk - QEMU run/debug targets.
#
# Direct boot is the default for every board, vexpress-a15 included: QEMU's
# own -kernel loader applies the ARM Linux boot protocol (r0=0, r1=machine
# id or ~0, r2=DTB physical address) to a *raw binary* image on every
# machine model tested (vexpress-a15, virt), not just boards with their own
# firmware. The only thing that ever actually required U-Boot here was a
# misdiagnosis: QEMU skips that register setup for ELF images, which made
# it look like vexpress-a15 needed a bootloader to get a DTB pointer at
# all. It doesn't — it needs a *raw* image and an explicit -dtb, exactly
# like every other board. See mk/uboot.mk for the (opt-in, off-by-default)
# real-bootloader path this replaces as the default.
#
# Requires: arch.mk (per-board QEMU_*), config.mk (TARGET/IMG/DTB_FILE),
# sdcard.mk (SD_IMG), initrd.mk (INITRD).

.PHONY: run run-bridged run-pcap debug
.PHONY: run-direct run-direct-bridged run-direct-pcap debug-direct

PCAP_FILE ?= /tmp/zuzu.pcap

QEMU_MACHINE = $(QEMU_MACH_$(BOARD))
QEMU_CPU     = $(QEMU_CPU_$(BOARD))
QEMU_BIN     = $(if $(QEMU_BIN_$(BOARD)),$(QEMU_BIN_$(BOARD)),qemu-system-arm)
QEMU_MEM     = $(if $(QEMU_MEM_$(BOARD)),$(QEMU_MEM_$(BOARD)),64M)
# Board NIC flags; empty for boards whose NIC QEMU does not emulate.
QEMU_NET     = $(QEMU_NET_$(BOARD))
# Bridged/pcap networking additionally need a bare NIC model name (not a
# full -nic/-net flag string like QEMU_NET above) to build their own
# flags around -- only boards with a matching userspace driver define one.
# Checked, not silently substituted, so a board without one fails loudly
# instead of emulating hardware it has no driver for.
QEMU_NIC_MODEL = $(QEMU_NIC_MODEL_$(BOARD))
define check-qemu-nic-model
	@if [ -z "$(QEMU_NIC_MODEL)" ]; then \
		echo "  ERROR   BOARD '$(BOARD)' has no QEMU_NIC_MODEL_$(BOARD) set" \
		     "in its board.mk -- bridged/pcap networking needs a NIC model" \
		     "matching a userspace driver this board actually has." \
		     "Direct 'make run' works without one."; \
		exit 1; \
	fi
endef
# What QEMU boots: the raw image by default (QEMU only applies the ARM boot
# protocol registers to raw images, not ELFs), a board-specific override
# only if one is ever actually needed.
QEMU_KERNEL  = $(if $(QEMU_KERNEL_$(BOARD)),$(QEMU_KERNEL_$(BOARD)),$(IMG))
# SD card drive; skipped for boards with no matching disk interface (e.g.
# virt has no SD/MMC controller QEMU exposes as if=sd here).
ifeq ($(QEMU_NO_DRIVE_$(BOARD)),y)
QEMU_DRIVE =
else
QEMU_DRIVE = -drive file=$(SD_IMG),if=sd,format=raw
endif

# Flags shared by every direct-boot run/debug variant; each target adds
# -kernel + extras. -initrd is what actually gets the kernel an initrd on
# this path: QEMU patches the DTB it hands the guest with /chosen
# linux,initrd-start/-end pointing at wherever it placed the file (verified
# by reading guest memory: it sits right before the DTB, no overlap), and
# dtb_get_chosen_initrd() (kernel/dtb/dtb.c) already reads exactly that --
# it just never had anything to read on this path before.
QEMU_ARGS = -M $(QEMU_MACHINE) -cpu $(QEMU_CPU) -m $(QEMU_MEM) \
            -dtb $(DTB_FILE) -initrd $(INITRD) -nographic $(QEMU_DRIVE)

run:         run-direct
run-bridged: run-direct-bridged
run-pcap:    run-direct-pcap
debug:       debug-direct

run-direct: $(QEMU_KERNEL) $(DTB_FILE) $(INITRD)
	@echo "  QEMU    $(QEMU_KERNEL)"
	@$(QEMU_BIN) $(QEMU_ARGS) -kernel $(QEMU_KERNEL) $(QEMU_NET)

run-direct-bridged: $(QEMU_KERNEL) $(DTB_FILE) $(INITRD)
	$(call check-qemu-nic-model)
	@echo "  QEMU    $(QEMU_KERNEL) [bridged]"
	@sudo $(QEMU_BIN) $(QEMU_ARGS) -kernel $(QEMU_KERNEL) \
	    -nic vmnet-bridged,model=$(QEMU_NIC_MODEL),ifname=en0,mac=52:54:00:ab:cd:ef

run-direct-pcap: $(QEMU_KERNEL) $(DTB_FILE) $(INITRD)
	$(call check-qemu-nic-model)
	@echo "  QEMU    $(QEMU_KERNEL) [pcap -> $(PCAP_FILE)]"
	@$(QEMU_BIN) $(QEMU_ARGS) -kernel $(QEMU_KERNEL) \
	    -net nic,model=$(QEMU_NIC_MODEL) -net user,id=n0 \
	    -object filter-dump,id=f0,netdev=n0,file=$(PCAP_FILE)
	@echo "  PCAP    wrote $(PCAP_FILE) (read with: tcpdump -nr $(PCAP_FILE))"

# $(TARGET) (the ELF) is never what QEMU boots here -- it only applies the
# ARM boot protocol to raw images -- but it's kept as a prerequisite so
# it's on disk for `arm-none-eabi-gdb build/zuzu.elf` to pull symbols from
# before attaching (see README's Debug section).
debug-direct: $(QEMU_KERNEL) $(TARGET) $(DTB_FILE) $(INITRD)
	@echo "  QEMU    $(QEMU_KERNEL) (debug)"
	@$(QEMU_BIN) $(QEMU_ARGS) -kernel $(QEMU_KERNEL) $(QEMU_NET) -S -gdb tcp::1234

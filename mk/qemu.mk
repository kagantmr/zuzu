# mk/qemu.mk - QEMU run/debug targets.
#
# vexpress-a15 always boots through U-Boot: the kernel no longer supports a
# no-bootloader path (real vexpress hardware wouldn't have one either. A
# board with its own firmware, like rpi4, hands off DTB/initrd directly and
# never needed this). run/run-bridged/run-pcap/debug all resolve to their
# *-uboot variants for it (see uboot.mk). Boards without a U-Boot
# integration keep booting straight off the ELF/raw image via QEMU's
# -kernel (the *-direct variants here).
#
# Requires: arch.mk (per-board QEMU_*), config.mk (TARGET/DTB_FILE),
# sdcard.mk (SD_IMG).

.PHONY: run run-bridged run-pcap debug
.PHONY: run-direct run-direct-bridged run-direct-pcap debug-direct

PCAP_FILE ?= /tmp/zuzu.pcap

QEMU_MACHINE = $(QEMU_MACH_$(BOARD))
QEMU_CPU     = $(QEMU_CPU_$(BOARD))
QEMU_BIN     = $(if $(QEMU_BIN_$(BOARD)),$(QEMU_BIN_$(BOARD)),qemu-system-arm)
QEMU_MEM     = $(if $(QEMU_MEM_$(BOARD)),$(QEMU_MEM_$(BOARD)),64M)
# Board NIC flags; empty for boards whose NIC QEMU does not emulate.
QEMU_NET     = $(QEMU_NET_$(BOARD))
# What QEMU boots: the ELF by default, a raw image where the board needs it.
QEMU_KERNEL  = $(if $(QEMU_KERNEL_$(BOARD)),$(QEMU_KERNEL_$(BOARD)),$(TARGET))

# Flags shared by every direct-boot run/debug variant; each target adds
# -kernel + extras.
QEMU_ARGS = -M $(QEMU_MACHINE) -cpu $(QEMU_CPU) -m $(QEMU_MEM) \
            -dtb $(DTB_FILE) -nographic -drive file=$(SD_IMG),if=sd,format=raw

ifeq ($(BOARD),vexpress-a15)
run:         run-uboot
run-bridged: run-uboot-bridged
run-pcap:    run-uboot-pcap
debug:       debug-uboot
else
run:         run-direct
run-bridged: run-direct-bridged
run-pcap:    run-direct-pcap
debug:       debug-direct
endif

run-direct: $(QEMU_KERNEL) $(DTB_FILE)
	@echo "  QEMU    $(QEMU_KERNEL)"
	@$(QEMU_BIN) $(QEMU_ARGS) -kernel $(QEMU_KERNEL) $(QEMU_NET)

run-direct-bridged: $(TARGET) $(DTB_FILE)
	@echo "  QEMU    $(TARGET) [bridged]"
	@sudo $(QEMU_BIN) $(QEMU_ARGS) -kernel $(TARGET) \
	    -nic vmnet-bridged,model=lan9118,ifname=en0,mac=52:54:00:ab:cd:ef

run-direct-pcap: $(TARGET) $(DTB_FILE)
	@echo "  QEMU    $(TARGET) [pcap -> $(PCAP_FILE)]"
	@$(QEMU_BIN) $(QEMU_ARGS) -kernel $(TARGET) \
	    -net nic,model=lan9118 -net user,id=n0 \
	    -object filter-dump,id=f0,netdev=n0,file=$(PCAP_FILE)
	@echo "  PCAP    wrote $(PCAP_FILE) (read with: tcpdump -nr $(PCAP_FILE))"

debug-direct: $(TARGET) $(DTB_FILE)
	@echo "  QEMU    $(TARGET) (debug)"
	@$(QEMU_BIN) $(QEMU_ARGS) -kernel $(TARGET) $(QEMU_NET) -S -gdb tcp::1234

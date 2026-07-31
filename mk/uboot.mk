# mk/uboot.mk - U-Boot integration (vexpress-a15).
#
# U-Boot's bootm loads the legacy "uImage" payload at its embedded load
# address and jumps to its embedded entry address using the ARM Linux boot
# register convention (r0=0, r1=machine id, r2=FDT). Load address is the
# board's fixed BOOT_PA (arch/arm/vexpress-a15/linker.ld); entry address is
# wherever _start actually lands, which drifts as .text.boot grows/shrinks
# and is re-extracted from the ELF on every image rebuild below.
#
# Requires: host.mk (HOST_OS/NPROC), config.mk (TARGET/IMG), initrd.mk
# (INITRD), qemu.mk (QEMU_MACHINE/CPU/MEM/NET/BIN), sdcard.mk (sdimg-stage,
# SD_STAGE_DIR, MAKE_FAT_IMAGE).

UBOOT_SRC_DIR     ?= uboot-src
UBOOT_DEFCONFIG   ?= vexpress_ca15_tc2_defconfig
UBOOT_MKIMAGE     ?= mkimage
UBOOT_BIN         ?= $(UBOOT_SRC_DIR)/u-boot
UBOOT_IMG         ?= build/zuzu.uImage
UBOOT_INITRD_IMG  ?= build/initrd.uImage
UBOOT_BOOT_CMD    ?= scripts/uboot-vexpress.cmd
UBOOT_BOOT_SCR    ?= build/boot.scr
UBOOT_SD_IMG      ?= build/sd-uboot.img
UBOOT_KERNEL_LOAD ?= 0x8000c000

.PHONY: uboot uimage uboot-stage sdimg-uboot run-uboot
.PHONY: debug-uboot run-uboot-bridged run-uboot-pcap

# Builds uboot-src/u-boot if the source tree is present. Not a prerequisite
# of $(UBOOT_BIN) itself, since a pre-built binary at UBOOT_BIN is enough
# to run/debug zuzu with; run this explicitly (or after cloning u-boot).
uboot:
	@if [ ! -d $(UBOOT_SRC_DIR) ]; then \
	    echo "  UBOOT   $(UBOOT_SRC_DIR) not found; clone u-boot there first, e.g.:"; \
	    echo "          git clone --depth 1 -b v2019.01 https://source.denx.de/u-boot/u-boot.git $(UBOOT_SRC_DIR)"; \
	    exit 1; \
	fi
	@if [ ! -f $(UBOOT_SRC_DIR)/.config ]; then \
	    echo "  CONFIG  $(UBOOT_DEFCONFIG)"; \
	    $(MAKE) -C $(UBOOT_SRC_DIR) CROSS_COMPILE=$(CROSS) $(UBOOT_DEFCONFIG); \
	fi
	@# vexpress_common.h's CONFIG_VEXPRESS_EXTENDED_MEMORY_MAP defaults
	@# scriptaddr/pxefile_addr_r to 0xa8000000 (~672M into RAM) — sized for
	@# real vexpress hardware, not our 64M QEMU instance. That's a U-Boot
	@# staging address only, unrelated to what zuzu itself needs, so fix it
	@# here rather than growing QEMU's -m to cover it.
	@echo "  PATCH   scriptaddr/pxefile_addr_r (vexpress_common.h)"
	@sed -i.bak 's/=0xa8000000/=0x80100000/g' \
	    $(UBOOT_SRC_DIR)/include/configs/vexpress_common.h
	@rm -f $(UBOOT_SRC_DIR)/include/configs/vexpress_common.h.bak
	@echo "  MAKE    $(UBOOT_BIN)"
	@# u-boot's own build is noisy (its own per-file CC/LD echoes) and, on
	@# this release, ends with a CFGCHK lint step (Kconfig-migration
	@# bookkeeping) that fails even when the actual binary links fine —
	@# CONFIG_SYS_MALLOC_CLEAR_ON_INIT is Kconfig-derived but not yet in
	@# u-boot's own migration whitelist at v2019.01. That step runs after
	@# $(UBOOT_BIN) is already built, so judge success by whether the
	@# binary exists, not by u-boot's own output/exit code — logged to
	@# build/uboot-build.log, only shown if the binary is truly missing.
	@mkdir -p build
	@if [ "$(HOST_OS)" = "Darwin" ]; then \
	    $(MAKE) -C $(UBOOT_SRC_DIR) CROSS_COMPILE=$(CROSS) HOSTLDFLAGS="-Wl,-ld_classic" \
	        -j$(NPROC) > build/uboot-build.log 2>&1 || true; \
	else \
	    $(MAKE) -C $(UBOOT_SRC_DIR) CROSS_COMPILE=$(CROSS) -j$(NPROC) > build/uboot-build.log 2>&1 || true; \
	fi
	@if [ ! -f $(UBOOT_BIN) ]; then \
	    echo "  UBOOT   build failed: $(UBOOT_BIN) was not produced; see build/uboot-build.log"; \
	    tail -40 build/uboot-build.log; \
	    exit 1; \
	fi

$(UBOOT_IMG): $(IMG) $(TARGET)
	$(call check-tool,$(UBOOT_MKIMAGE),install u-boot-tools (e.g. apt install u-boot-tools / brew install u-boot-tools).)
	@mkdir -p $(dir $@)
	@entry=$$($(OBJDUMP) -f $(TARGET) | awk '/start address/ {print $$NF}'); \
	echo "  MKIMAGE $@ (load $(UBOOT_KERNEL_LOAD), entry $$entry)"; \
	$(UBOOT_MKIMAGE) -A arm -O linux -T kernel -C none \
	    -a $(UBOOT_KERNEL_LOAD) -e $$entry \
	    -n "zuzuOS" -d $(IMG) $@ >/dev/null

$(UBOOT_INITRD_IMG): $(INITRD)
	@mkdir -p $(dir $@)
	@echo "  MKIMAGE $@"
	@$(UBOOT_MKIMAGE) -A arm -O linux -T ramdisk -C none \
	    -n "zuzuOS initrd" -d $(INITRD) $@ >/dev/null

$(UBOOT_BOOT_SCR): $(UBOOT_BOOT_CMD)
	@mkdir -p $(dir $@)
	@echo "  MKIMAGE $@"
	@$(UBOOT_MKIMAGE) -A arm -O linux -T script -C none \
	    -n "zuzuOS vexpress boot" -d $< $@ >/dev/null

uimage: $(UBOOT_IMG) $(UBOOT_INITRD_IMG) $(UBOOT_BOOT_SCR)

uboot-stage: sdimg-stage uimage $(DTB_FILE)
	@mkdir -p $(SD_STAGE_DIR)/boot
	@cp $(UBOOT_IMG) $(SD_STAGE_DIR)/boot/zuzu.uImage
	@cp $(UBOOT_INITRD_IMG) $(SD_STAGE_DIR)/boot/initrd.uImage
	@cp $(UBOOT_BOOT_SCR) $(SD_STAGE_DIR)/boot/boot.scr
	@cp $(DTB_FILE) $(SD_STAGE_DIR)/boot/vexpress-v2p-ca15-tc1.dtb
	@echo "  STAGE   $(SD_STAGE_DIR)/boot/{zuzu.uImage,initrd.uImage,boot.scr,vexpress-v2p-ca15-tc1.dtb}"

$(UBOOT_SD_IMG): uboot-stage
	$(call MAKE_FAT_IMAGE,$(UBOOT_SD_IMG),$(SD_STAGE_DIR))

sdimg-uboot: $(UBOOT_SD_IMG)

# Flags shared by every U-Boot run/debug variant; each target adds net +
# extras (mirrors QEMU_ARGS's split for the direct-boot variants in qemu.mk).
QEMU_UBOOT_ARGS = -M $(QEMU_MACHINE) -cpu $(QEMU_CPU) -m $(QEMU_MEM) \
                  -nographic -kernel $(UBOOT_BIN) \
                  -drive file=$(UBOOT_SD_IMG),if=sd,format=raw

run-uboot: sdimg-uboot $(UBOOT_BIN)
	@echo "  QEMU    $(UBOOT_BIN) -> $(UBOOT_SD_IMG)"
	@$(QEMU_BIN) $(QEMU_UBOOT_ARGS) $(QEMU_NET)

run-uboot-bridged: sdimg-uboot $(UBOOT_BIN)
	@echo "  QEMU    $(UBOOT_BIN) -> $(UBOOT_SD_IMG) [bridged]"
	@sudo $(QEMU_BIN) $(QEMU_UBOOT_ARGS) \
	    -nic vmnet-bridged,model=lan9118,ifname=en0,mac=52:54:00:ab:cd:ef

run-uboot-pcap: sdimg-uboot $(UBOOT_BIN)
	@echo "  QEMU    $(UBOOT_BIN) -> $(UBOOT_SD_IMG) [pcap -> $(PCAP_FILE)]"
	@$(QEMU_BIN) $(QEMU_UBOOT_ARGS) \
	    -net nic,model=lan9118 -net user,id=n0 \
	    -object filter-dump,id=f0,netdev=n0,file=$(PCAP_FILE)
	@echo "  PCAP    wrote $(PCAP_FILE) (read with: tcpdump -nr $(PCAP_FILE))"

# GDB won't survive a plain breakpoint on kernel code here: U-Boot's bootm
# copies the raw zuzu.img over that RAM *after* GDB attaches, silently
# wiping any software breakpoint already written there. Use a hardware
# breakpoint instead (`hbreak <symbol>` in GDB, or via setupCommands in
# .vscode/launch.json) — it's a PC compare in a debug register, so it
# survives the copy regardless of when it was set.
debug-uboot: sdimg-uboot $(UBOOT_BIN)
	@echo "  QEMU    $(UBOOT_BIN) -> $(UBOOT_SD_IMG) (debug)"
	@$(QEMU_BIN) $(QEMU_UBOOT_ARGS) $(QEMU_NET) -S -gdb tcp::1234

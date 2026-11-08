# bsp/aarch64/rpi4/board.mk - Raspberry Pi 4 board metadata.

DTB_rpi4        = bsp/aarch64/rpi4/bcm2711-rpi-4-b.dtb
QEMU_MACH_rpi4  = raspi4b
QEMU_CPU_rpi4   = cortex-a72
QEMU_BIN_rpi4   = qemu-system-aarch64
QEMU_MEM_rpi4   = 2G
# if=sd lands on the legacy SDHCI; the card slot (and emmc2drv) is EMMC2, the first generic-sdhci
QEMU_DRIVE_rpi4 = -drive file=$(SD_IMG),if=none,format=raw,id=sdcard \
                  -device sd-card,drive=sdcard,bus=/generic-sdhci/sd-bus

CPUFLAGS_rpi4   = -mcpu=cortex-a72  -falign-functions=64

BOOT_FILES_rpi4  = bsp/aarch64/rpi4/config.txt $(IMG) $(INITRD) $(DTB_FILE)

# Closed-source Pi firmware (start4.elf, fixup4.dat) is not vendored. Drop it
# here, or leave it on the card and only copy the files above.
BOOT_FW_DIR_rpi4 = firmware

# mk/dtb.mk - device tree blobs compiled from checked-in sources (QEMU
# only; real hardware boots with the firmware-provided DTB).

build/dtb/%.dtb: %.dts
	$(call check-tool,dtc,install it via your package manager (e.g. apt install device-tree-compiler / brew install dtc).)
	@mkdir -p $(dir $@)
	@echo "  DTC     $@"
	@dtc -I dts -O dtb -o $@ $<

# Raw kernel image for real hardware (e.g. kernel= in the Pi 4 config.txt).
# IMG itself is defined in config.mk.
$(IMG): $(TARGET)
	@echo "  OBJCOPY $@"
	@$(OBJCOPY) -O binary $(TARGET) $(IMG)

.PHONY: img
img: $(IMG)

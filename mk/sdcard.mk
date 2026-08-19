# mk/sdcard.mk - SD card FAT32 image workflow.
#
#  Typical workflow:
#    make                  — build kernel + all programs
#    make sdimg            — stage DISK_PROGS and create sd.img from ZUZUSD/
#    make run              — launch QEMU
#
#  ZUZUSD/ is the staging directory. Put non-generated resources (headers,
#  config files, vendored data) there directly and they'll be tracked in
#  git and shipped on the SD card. Compiled output (ZUZUSD/bin/ ELFs,
#  ZUZUSD/lib/*.a, ZUZUSD/boot/) is regenerated from source by this target
#  and gitignored — see .gitignore.
#
#  To update the SD card after code changes:
#    make sdimg-recreate && make run
#
# Requires: host.mk (HOST_OS), user.mk (SD_PROGS/SD_LIBS/USER_CRT0/ZCRT_ARCHIVE).

SD_IMG         ?= build/sd.img
SD_IMG_SIZE_MB ?= 64
SD_VOL_LABEL   ?= ZUZU
SD_STAGE_DIR   ?= ZUZUSD

.PHONY: sdimg sdimg-stage sdimg-clean sdimg-recreate

sdimg-stage: $(SD_PROG_PACKED_ELFS) $(SD_LIB_ARCHIVES) $(USER_CRT0) $(ZCRT_ARCHIVE)
	@rm -rf $(SD_STAGE_DIR)/bin $(SD_STAGE_DIR)/lib $(SD_STAGE_DIR)/include
	@mkdir -p $(SD_STAGE_DIR)/bin
	@mkdir -p $(SD_STAGE_DIR)/lib
	@mkdir -p $(SD_STAGE_DIR)/include
	@for prog in $(SD_PROGS); do \
		cp build/user/$$prog.stripped.elf $(SD_STAGE_DIR)/bin/$$prog; \
		echo "  STAGE   $(SD_STAGE_DIR)/bin/$$prog"; \
	done
	@for lib in $(SD_LIBS); do \
		cp build/user/lib/$$lib.a $(SD_STAGE_DIR)/lib/$$lib.a; \
		echo "  STAGE   $(SD_STAGE_DIR)/lib/$$lib.a"; \
	done
	@# System headers and runtime binaries staged for TCC.
	@cp -r include/* $(SD_STAGE_DIR)/include/
	@cp $(USER_CRT0) $(SD_STAGE_DIR)/lib/crt0.o
	@cp $(ZCRT_ARCHIVE) $(SD_STAGE_DIR)/lib/libc.a
	@echo "  STAGE   $(SD_STAGE_DIR)/include/ and $(SD_STAGE_DIR)/lib/libc.a"

# Creates a FAT32 image ($(1)) from a staging directory ($(2)). macOS uses
# hdiutil; everywhere else uses mkfs.fat + mtools (dosfstools/mtools).
define MAKE_FAT_IMAGE
	@mkdir -p $(dir $(1))
	@rm -f $(1) $(1).dmg
	@echo "  IMG     $(1) from $(2)/ ($(SD_IMG_SIZE_MB)MB, FAT32)"
	@if [ "$(HOST_OS)" = "Darwin" ]; then \
	    COPYFILE_DISABLE=1 hdiutil create -srcfolder "$(2)" -fs "MS-DOS FAT32" \
	        -volname $(SD_VOL_LABEL) -size $(SD_IMG_SIZE_MB)m \
	        -format UDIF $(1) >/dev/null; \
	    mv $(1).dmg $(1); \
	    MNT=$$(mktemp -d); \
	    hdiutil attach $(1) -mountpoint "$$MNT" -nobrowse >/dev/null; \
	    dot_clean -m "$$MNT"; \
	    hdiutil detach "$$MNT" >/dev/null; \
	    rmdir "$$MNT"; \
	else \
	    if ! command -v mkfs.fat >/dev/null 2>&1; then \
	        echo "  IMG     failed: install dosfstools  (sudo apt install dosfstools)"; exit 1; \
	    fi; \
	    if ! command -v mcopy >/dev/null 2>&1; then \
	        echo "  IMG     failed: install mtools  (sudo apt install mtools)"; exit 1; \
	    fi; \
	    dd if=/dev/zero of=$(1) bs=1M count=$(SD_IMG_SIZE_MB) status=none; \
	    mkfs.fat -F 32 -n $(SD_VOL_LABEL) $(1) >/dev/null; \
	    mcopy -i $(1) -s $(2)/* ::; \
	fi
endef

$(SD_IMG): sdimg-stage
	$(call MAKE_FAT_IMAGE,$(SD_IMG),$(SD_STAGE_DIR))

sdimg: $(SD_IMG)

sdimg-clean:
	@rm -f $(SD_IMG)
	@echo "  CLEAN   $(SD_IMG)"

sdimg-recreate: sdimg-clean sdimg

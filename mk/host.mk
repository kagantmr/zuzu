# mk/host.mk - host OS detection and portability helpers.
#
# Everything here describes the *build host* (the machine running make),
# as opposed to arch.mk/config.mk which describe the *target* (the board
# being built for). Modules that need to branch on host OS (sdcard.mk,
# uboot.mk) or want a friendly error instead of a raw "command not found"
# (dtb.mk, initrd.mk, sdcard.mk, uboot.mk) include this first.

HOST_OS := $(shell uname -s)

ifeq ($(HOST_OS),Darwin)
NPROC := $(shell sysctl -n hw.ncpu)
else
NPROC := $(shell nproc)
endif

# check-tool,<binary>,<install-hint> - errors out with an actionable
# message if <binary> isn't on PATH. Called from recipes (not top-level),
# so it only fires when a target that actually needs the tool is built.
define check-tool
	@command -v $(1) >/dev/null 2>&1 || { \
		echo "  ERROR   '$(1)' not found. $(2)"; exit 1; \
	}
endef

# mk/compile_commands.mk - compile_commands.json generation for clangd.
#
# Reflects the currently selected $(ARCH)/$(BOARD): cheap enough to rerun
# after switching boards (`make BOARD=rpi4 compile_commands.json`).
#
# Uses bear's `parse-sh` mode against `make -n` dry-run text, not exec()
# interception (bear's more common `bear -- make` mode): NEWLIB_CC
# (mk/toolchain.mk) resolves to an absolute path outside PATH whenever the
# plain `arm-none-eabi-gcc` on PATH doesn't bundle newlib (true on this
# machine's Homebrew install) -- exec-interception only works when the
# compiler is invoked via a bare name Make looks up on PATH, so tier-2
# compiles were silently missing from the output entirely (confirmed:
# 0 newlib/posix entries) under `bear -- make`. `make -n`'s dry-run text
# is the fully-expanded command line regardless of whether it came from a
# PATH-resolved name or an absolute path, so parse-sh has no such gap
# (confirmed: correctly captures both compiler populations). It's also a
# pure text parse, not an actual build, so it's a lot faster and needs no
# LD_PRELOAD/wrapper interception machinery at all -- nothing macOS/SIP-
# specific to worry about either.
#
# -B/--always-make is required, not optional: without it, `make -n` skips
# printing recipes for objects already up to date, which could mean none
# at all if a normal build already ran. `all` already builds kernel + all
# three tiers (see Makefile's own header comment), so one -n -B all pass
# prints every compiler invocation across every tier in a single dry run.
#
# Requires: host.mk (check-tool).

.PHONY: compile_commands.json
compile_commands.json:
	$(call check-tool,bear,install it via your package manager (e.g. apt/brew/pacman install bear).)
	@echo "  BEAR    compile_commands.json"
	@$(MAKE) -n -B all | bear parse-sh -o compile_commands.json

# mk/compile_commands.mk - compile_commands.json for clangd.
#
# Built from the .cmd files each compile rule drops next to its object (see
# record-cmd in config.mk), so the flags are make's own, not a reconstruction.
# Depends on `all` because only objects that were actually built have a .cmd.
#
# Covers the currently selected board only: clangd reads a single database, and
# merging boards would leave it picking whichever entry it saw first. Re-run
# after switching boards.

.PHONY: compile_commands.json
compile_commands.json: all
	$(call check-tool,python3,install Python 3.)
	@python3 scripts/ccjson.py $(O) $@

# kilo (ported)

`main.c` in this directory is a port of [antirez/kilo](https://github.com/antirez/kilo),
Salvatore Sanfilippo's small VT100 terminal text editor, adapted to run on zuzuOS.

- Upstream: https://github.com/antirez/kilo
- Author: Salvatore Sanfilippo (antirez)
- License: BSD 2-Clause -- see the header comment at the top of `main.c`, preserved
  unchanged from upstream. That license, not this file, governs the editor code.

## What's zuzuOS-specific

Everything outside the block marked `/* zuzuOS-specific handlers */` in `main.c`
is unmodified (or only lightly adapted) upstream kilo. The zuzuOS-specific
additions are:

- `getline()` — zuzuOS's libc doesn't provide POSIX `getline()`, so a minimal
  implementation is included here for `editorOpen()` to use.
- `enableRawMode()` / `disableRawMode()` -- zuzuOS has no termios layer
  (`tcgetattr`/`tcsetattr`). These call `zuzu_console_set_raw()` in
  `user/lib/posix/stubs.c` instead of manipulating a real termios struct.

No other functional changes were made to the editor logic itself.

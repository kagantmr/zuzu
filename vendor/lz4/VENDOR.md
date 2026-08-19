# Vendored: LZ4

Version:   1.10.0
Source:    https://github.com/lz4/lz4/ (tag v1.10.0, commit ebb370ca83af193212df4dcbadcc5d87bc0de2f0)
Retrieved: 2026-08-18
License:   BSD 2-Clause — see LICENSE in this directory (upstream lib/LICENSE,
           NOT this repo's own root LICENSE, which is MIT and covers zuzu itself)

## Files
Imported: lz4.h, lz4.c
Excluded: lz4hc.*, lz4frame.*, xxhash.*

## Local modifications
None. Files are byte-identical to upstream; keep it that way so upgrades
stay a re-download rather than a merge.

## Build configuration

  LZ4_FREESTANDING=1
      Drops libc dependencies. Requires LZ4_memcpy / LZ4_memmove /
      LZ4_memset to be supplied by us — done via -D on this object only
      (mk/kernel.mk: LZ4_memcpy=memcpy etc., plus -include string.h so the
      klib/mem.c prototypes are in scope; LZ4_FREESTANDING skips lz4.c's
      own <string.h> include, so without -include the memcpy/memset/
      memmove calls have no declaration in scope).

  LZ4_FORCE_MEMORY_ACCESS=0
      DO NOT REMOVE. Forces unaligned reads through memcpy. LZ4 otherwise
      auto-detects __ARM_FEATURE_UNALIGNED (defined by GCC for
      -mcpu=cortex-a72) and uses direct unaligned pointer access. The
      compiler cannot know that zuzu boots with SCTLR.A set, making
      unaligned access an alignment fault. Only revisit if SCTLR.A is
      ever cleared.

## Usage
LZ4_decompress_safe() only, for initrd decompression. The frame format is not used; zuzu prepends its own header carrying the
uncompressed size.
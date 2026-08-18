# Vendored: LZ4

Version:   1.10.0
Source:    https://github.com/lz4/lz4/ (tag v1.10.0, commit <hash>)
Retrieved: 2026-08-18
License:   BSD 2-Clause — see LICENSE in this directory (upstream lib/LICENSE,
           NOT the repo-root LICENSE, which is GPL-2.0 and covers the CLI tools)

## Files
Imported: lz4.h, lz4.c
Excluded: lz4hc.*, lz4frame.*, xxhash.*

## Local modifications
None. Files are byte-identical to upstream; keep it that way so upgrades
stay a re-download rather than a merge.

## Build configuration

  LZ4_FREESTANDING=1
      Drops libc dependencies. Requires LZ4_memcpy / LZ4_memmove /
      LZ4_memset to be supplied by us — defined in <path>.

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
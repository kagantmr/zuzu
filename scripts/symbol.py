#!/usr/bin/env python3
"""Emit the kernel symbol table (core/ksym.h) from a linked ELF.

    symbol.py [--nm BINARY] <kernel.elf> <out.c>
"""

import argparse
import subprocess
import sys


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nm", default="arm-none-eabi-nm",
                    help="nm binary to use (pass $(CROSS)nm)")
    ap.add_argument("elf")
    ap.add_argument("out")
    args = ap.parse_args()

    try:
        r = subprocess.run([args.nm, "--numeric-sort", args.elf],
                           capture_output=True, text=True)
    except OSError as e:
        sys.exit(f"symbol.py: cannot run {args.nm}: {e}")
    if r.returncode != 0:
        sys.exit(f"symbol.py: {args.nm} failed on {args.elf}:\n{r.stderr.strip()}")

    symbols = []
    for entry in r.stdout.splitlines():
        parts = entry.split()
        if len(parts) != 3:
            continue
        address, sym_type, name = parts
        if sym_type in ("T", "t"):
            symbols.append((address, name))

    if not symbols:
        sys.exit(f"symbol.py: no text symbols found in {args.elf}")

    with open(args.out, "w") as f:
        f.write('#include <stdint.h>\n')
        f.write('#include "core/ksym.h"\n')
        f.write('static const ksym_entry_t ksym_entries[] = {\n')
        for address, name in symbols:
            f.write(f'    {{ 0x{address}, "{name}" }},\n')
        f.write('};\n')
        f.write('const ksym_entry_t *ksym_table = ksym_entries;\n')
        f.write(f'volatile const uint32_t ksym_count = {len(symbols)};\n')


if __name__ == "__main__":
    main()

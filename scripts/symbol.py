#!/usr/bin/env python3

import subprocess
import sys

result = subprocess.run(['arm-none-eabi-nm', '--numeric-sort', sys.argv[1]], capture_output=True, text=True)

symbols = []
for entry in result.stdout.splitlines():
    parts = entry.split()
    if len(parts) != 3:
        continue
    address, sym_type, name = parts
    if sym_type in ['T', 't']:
        symbols.append((address, name))

with open(sys.argv[2], 'w') as f:
    f.write('#include <stdint.h>\n')
    f.write('#include "core/ksym.h"\n')
    f.write('static const ksym_entry_t ksym_entries[] = {\n')
    for address, name in symbols:
        f.write(f'    {{ 0x{address}, "{name}" }},\n')
    f.write('};\n')
    f.write('const ksym_entry_t *ksym_table = ksym_entries;\n')
    f.write(f'volatile const uint32_t ksym_count = {len(symbols)};\n')
from elftools.elf.elffile import ELFFile
import struct
import sys
import argparse
import hashlib, zlib
import os

parser = argparse.ArgumentParser(
                    prog='elf2zxf',
                    description='This script will convert ELF files into ZXF files.')

parser.add_argument("filename")
parser.add_argument("-o", "--output")

args = parser.parse_args()

with open(args.filename, 'rb') as f:
    elf = ELFFile(f)
    if elf.header['e_type'] != 'ET_EXEC' or elf.header['e_machine'] != 'EM_ARM' or elf.elfclass != 32:
        print("Incompatible file type")
        sys.exit(1)
    segs = [s for s in elf.iter_segments() if s['p_type'] == 'PT_LOAD']
    for s in segs:
        print(hex(s['p_vaddr']), s['p_filesz'], s['p_memsz'], s['p_flags'], s['p_align'])
        print("align:      ", s['p_align'].bit_length() - 1)
    image_size = max(x['p_vaddr'] + x['p_memsz'] for x in segs) - min(x['p_vaddr'] for x in segs)
    load_base  = min(s['p_vaddr'] for s in segs)
    entry = elf.header['e_entry']
    hdr = struct.pack('<4sBBBBQQIHHIII16s4s',
                          b'\x7aZXF',   # magic
                          0x01,         # version
                          0x01,         # arch (ARMv7-A)
                          0x02,         # flags (STATIC)
                          0,            # reserved
                          entry,        # from elf.header['e_entry']
                          load_base,
                          image_size,
                          len(segs),    # seg_count
                          0,            # block_count
                          64,           # seg_table_offset
                          0,            # block_table_offset
                          0,            # checksum (filled later)
                          b'',          # build_id (16s zero-pads)
                          b'')          # reserved2 (4s zero-pads)
    seg_table = b''
    off = 64 + 24 * len(segs)
    for s in segs:
        zf = 0
        if s['p_flags'] & 0x4: zf |= 0x1   # R
        if s['p_flags'] & 0x2: zf |= 0x2   # W
        if s['p_flags'] & 0x1: zf |= 0x4   # X
        seg_table += struct.pack('<IIQIBBH',
                             off,                    # file_offset
                             s['p_filesz'],
                             s['p_vaddr'],
                             s['p_memsz'],
                             zf,
                             s['p_align'].bit_length() - 1,
                             0)                      # reserved
        off += s['p_filesz']
    seg_data = b''.join(x.data() for x in segs)
    buf = bytearray(hdr + seg_table + seg_data)
    # build_id: hash over buf with checksum+build_id already zero
    build_id = hashlib.sha256(buf).digest()[:16]
    print("Hash:", build_id.hex())
    buf[44:60] = build_id

    crc = zlib.crc32(buf) & 0xffffffff
    print("Csum:", hex(crc))
    struct.pack_into('<I', buf, 40, crc)

    out = args.output or os.path.splitext(args.filename)[0] + '.zxf'
    open(out, 'wb').write(buf)

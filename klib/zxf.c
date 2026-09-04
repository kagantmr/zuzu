#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <zuzu/zxf.h>


static uint32_t ZxfCrc32Partial(const void *data, size_t len, uint32_t crc) {
    const uint8_t *bytes = (const uint8_t *)data;
    for (size_t i = 0; i < len; i++) {
        crc ^= bytes[i];
        for (size_t j = 0; j < 8; j++) {
            if ((crc & 1)) {
                crc = (crc >> 1) ^ 0xEDB88320;
            } else {
                crc = crc >> 1;
            }
        }
    }
    return crc;
}

uint32_t ZxfCrc32(const void *data, size_t len) {
    return ZxfCrc32Partial(data, len, 0xFFFFFFFF) ^ 0xFFFFFFFF;
}

bool ZxfParse(const void *data, size_t size, ZXFImage *out) {

    const uint8_t zeros[4] = {0};

    if (size < sizeof(ZXFHeader)) return false;

    const ZXFHeader *hdr = (const ZXFHeader *)data;

    if (memcmp(hdr->magic, "\x7a" "ZXF", 4) != 0) return false;
    if (hdr->version != 0x01) return false;

    if (hdr->arch != ARM32) return false;
    
    const uint8_t *base = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFF;
    crc = ZxfCrc32Partial(base, 40, crc);
    crc = ZxfCrc32Partial(zeros, 4, crc);
    crc = ZxfCrc32Partial(base + 44, size - 44, crc);
    crc ^= 0xFFFFFFFF;

    if (crc != hdr->checksum) return false;

    if (hdr->seg_table_offset > size) return false;
    if (hdr->seg_count * sizeof(ZXFSegment) > size - hdr->seg_table_offset) return false;

    if (hdr->block_table_offset > size) return false;
    if (hdr->block_count * sizeof(ZXFBlock) > size - hdr->block_table_offset) return false;


    out->base = base;
    out->size = size;
    out->entry = (uint32_t)hdr->entry;
    out->seg_count = hdr->seg_count;
    /* base is the whole loaded image (page-aligned by every caller) and
     * seg_table_offset/block_table_offset are fixed struct-size multiples
     * from elf2zxf.py, so this pointer is actually aligned; the cast just
     * has to go through (const void *) to tell the compiler that. */
    out->segs = (const ZXFSegment *)(const void *)(base + hdr->seg_table_offset);

    bool blocktype_optional = false;
    for (int i = 0; i < hdr->block_count; i++) {
        /* Same alignment rationale as out->segs above. */
        const ZXFBlock *block = &((const ZXFBlock *)(const void *)(base + hdr->block_table_offset))[i];
        if (block->flags == REQUIRED) blocktype_optional = false;
        else if (block->flags == OPTIONAL) blocktype_optional = true;
        else return false;

        switch (block->type) {
            case DYNLINK:
            case HINTS:
            case DEBUGINFO:
            case SIGNATURE:
            default:
                if (!blocktype_optional) return false;
                else continue;
        }
    }

    return true;
}
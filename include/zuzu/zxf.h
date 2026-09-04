#ifndef ZXF_FORMAT_H
#define ZXF_FORMAT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ZXF_R 1
#define ZXF_W 2
#define ZXF_X 4

typedef struct
{
    uint8_t magic[4];
    uint8_t version;
    uint8_t arch;
    uint8_t flags;
    uint8_t reserved;
    uint64_t entry;
    uint64_t load_base;
    uint32_t image_size;
    uint16_t seg_count;
    uint16_t block_count;
    uint32_t seg_table_offset;
    uint32_t block_table_offset;
    uint32_t checksum;
    uint8_t build_id[16];
    uint8_t reserved2[4];
} ZXFHeader;

typedef struct
{
    uint32_t file_offset;
    uint32_t file_size;
    uint64_t vaddr;
    uint32_t mem_size;
    uint8_t flags;
    uint8_t align;
    uint16_t reserved;
} ZXFSegment;

typedef struct
{
    const uint8_t *base;
    size_t size;
    uint32_t entry;
    uint16_t seg_count;
    const ZXFSegment *segs;
} ZXFImage;

typedef enum
{
    ARM32 = 0x01,
    ARM64,
    X86_64,
    RISCV,
    POWPC
} ZXFArch;

typedef enum
{
    DYNLINK = 0x0001,   // a Zuzu Common Library (ZCL)
    HINTS = 0x0002,     // sysd hints
    DEBUGINFO = 0x0003, // debug symbols
    SIGNATURE = 0x0004  // driver signature
} ZXFBlockType;

typedef enum
{
    OPTIONAL = 0x0000,
    REQUIRED = 0x0001
} ZXFBlockFlag;

typedef struct
{
    uint16_t type;
    uint16_t flags;
    uint32_t offset;
    uint32_t size;
} ZXFBlock;

_Static_assert(sizeof(ZXFBlock) == 12, "ZXFBlock must be 12 bytes");
_Static_assert(sizeof(ZXFHeader) == 64, "ZXFHeader must be 64 bytes");
_Static_assert(sizeof(ZXFSegment) == 24, "ZXFSegment must be 24 bytes");

uint32_t ZxfCrc32(const void *data, size_t len);
bool ZxfParse(const void *data, size_t size, ZXFImage *out);

#endif /* ZXF_FORMAT_H */
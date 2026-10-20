/**
 * @file
 * @brief ZXF, zuzu's executable container: header, segment table and optional blocks.
 */

#ifndef ZXF_FORMAT_H
#define ZXF_FORMAT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief Segment flag: readable. */
#define ZXF_R 1
/** @brief Segment flag: writable. */
#define ZXF_W 2
/** @brief Segment flag: executable. */
#define ZXF_X 4

/** @brief ZXF file header (64 bytes, at offset 0). */
typedef struct {
    uint8_t magic[4];            /**< File magic. */
    uint8_t version;             /**< Format version. */
    uint8_t arch;                /**< Target architecture, a ZXFArch. */
    uint8_t flags;               /**< Image flags. */
    uint8_t reserved;            /**< Must be zero. */
    uint64_t entry;              /**< Entry point address. */
    uint64_t load_base;          /**< Lowest load address. */
    uint32_t image_size;         /**< Total file size in bytes. */
    uint16_t seg_count;          /**< Entries in the segment table. */
    uint16_t block_count;        /**< Entries in the block table. */
    uint32_t seg_table_offset;   /**< File offset of the segment table. */
    uint32_t block_table_offset; /**< File offset of the block table. */
    uint32_t checksum;           /**< Image checksum. */
    uint8_t build_id[16];        /**< Build identifier. */
    uint8_t reserved2[4];        /**< Must be zero. */
} ZXFHeader;

/** @brief One loadable segment (24 bytes). */
typedef struct {
    uint32_t file_offset; /**< Offset of the segment's bytes in the file. */
    uint32_t file_size;   /**< Bytes present in the file. */
    uint64_t vaddr;       /**< Load address. */
    uint32_t mem_size;    /**< Size in memory; the part beyond file_size is zero-filled. */
    uint8_t flags;        /**< ZXF_R / ZXF_W / ZXF_X. */
    uint8_t align;        /**< Alignment, as a power of two. */
    uint16_t reserved;    /**< Must be zero. */
} ZXFSegment;

/** @brief A parsed ZXF image, pointing into the caller's buffer. */
typedef struct {
    const uint8_t *base;    /**< Start of the image bytes. */
    size_t size;            /**< Image size in bytes. */
    uint32_t entry;         /**< Entry point address. */
    uint16_t seg_count;     /**< Number of segments. */
    const ZXFSegment *segs; /**< The segment table, inside the image. */
} ZXFImage;

/** @brief Target architecture of an image. */
typedef enum {
    ARM32 = 0x01, /**< 32-bit ARM. */
    ARM64,        /**< 64-bit ARM. */
    X86_64,       /**< x86-64. */
    RISCV,        /**< RISC-V. */
    POWPC         /**< PowerPC. */
} ZXFArch;

/** @brief Kind of an optional block. */
typedef enum {
    DYNLINK = 0x0001,   /**< A zuzu common library (ZCL). */
    HINTS = 0x0002,     /**< Loader hints. */
    DEBUGINFO = 0x0003, /**< Debug symbols. */
    SIGNATURE = 0x0004  /**< Signature. */
} ZXFBlockType;

/** @brief Whether a loader may ignore a block it does not understand. */
typedef enum {
    OPTIONAL = 0x0000, /**< Safe to ignore. */
    REQUIRED = 0x0001  /**< Must be understood, or the image is rejected. */
} ZXFBlockFlag;

/** @brief Entry in the block table (12 bytes). */
typedef struct {
    uint16_t type;   /**< A ZXFBlockType. */
    uint16_t flags;  /**< A ZXFBlockFlag. */
    uint32_t offset; /**< File offset of the block. */
    uint32_t size;   /**< Block size in bytes. */
} ZXFBlock;

_Static_assert(sizeof(ZXFBlock) == 12, "ZXFBlock must be 12 bytes");
_Static_assert(sizeof(ZXFHeader) == 64, "ZXFHeader must be 64 bytes");
_Static_assert(sizeof(ZXFSegment) == 24, "ZXFSegment must be 24 bytes");

/**
 * @brief Validates a ZXF image and describes it.
 *
 * @param[in]  data Image bytes.
 * @param[in]  size Image size in bytes.
 * @param[out] out  Filled on success; points into @p data.
 * @return true if @p data is a valid ZXF image.
 */
bool ZxfParse(const void *data, size_t size, ZXFImage *out);

#endif /* ZXF_FORMAT_H */
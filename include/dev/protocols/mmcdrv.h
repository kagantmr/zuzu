/**
 * @file
 * @brief Block-device protocol shared by every SD/MMC driver ("/dev/mmc0").
 *
 * One client at a time. Every request gets exactly one reply, sent once the
 * transfer has finished. Requests and replies travel in the message buffer;
 * block data goes through the buffer page from SD_CMD_GET_BUF.
 */

#ifndef SD_PROTOCOL_H
#define SD_PROTOCOL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <zuzu/err.h>

/**
 * @brief Get the transfer buffer. SdRequest {cmd}; replies SdReply and grants the
 * driver-owned buffer page (PERM_MAP | PERM_WRITE | PERM_TXFR). Asking again grants the
 * same buffer.
 */
#define SD_CMD_GET_BUF 1
/** @brief Read blocks. SdRequest {cmd, lba, count}; the data lands at buffer offset 0. */
#define SD_CMD_READ 2
/** @brief Write blocks. SdRequest {cmd, lba, count}; the data is taken from buffer offset 0. */
#define SD_CMD_WRITE 3

/** @brief A block transfer failed: CRC error, timeout, or FIFO overrun or underrun. */
#define SD_ERR_IO ERR_IO

/** @brief Bytes per block. */
#define SD_BLOCK_SIZE 512u
/** @brief Size of the transfer buffer: 16 pages, so at most 128 blocks per request. */
#define SD_BUF_SIZE 65536u

/** @brief A request, in the message buffer. */
typedef struct {
    uint32_t cmd;   /**< An SD_CMD_* value. */
    uint32_t lba;   /**< First block. */
    uint32_t count; /**< Number of blocks. */
} SdRequest;

/** @brief A reply, in the message buffer; READ and WRITE fill in only @c status. */
typedef struct {
    Err status;           /**< ZUZU_OK, or an error code. */
    uint32_t buf_size;    /**< GET_BUF: size of the transfer buffer. */
    uint32_t block_size;  /**< GET_BUF: bytes per block. */
    uint32_t block_count; /**< GET_BUF: blocks on the card, or 0 if unknown. */
} SdReply;

#ifdef __cplusplus
}
#endif

#endif /* SD_PROTOCOL_H */

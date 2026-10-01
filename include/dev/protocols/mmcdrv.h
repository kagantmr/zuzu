#ifndef SD_PROTOCOL_H
#define SD_PROTOCOL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <zuzu/err.h>

/*
 * Block-device protocol shared by every SD/MMC driver ("/dev/mmc0").
 * One client at a time; every request is answered with exactly one reply and
 * is synchronous: the reply is sent once the transfer has finished. All
 * payloads are written to / read from MessageBuf().
 */

#define SD_CMD_GET_BUF 1 /* req {cmd}. reply {Err status; u32 buf_size; u32 block_size; u32 block_count}
                            and GRANTS a dup (PERM_MAP|PERM_TXFR) of the driver-owned buffer page.
                            A second GET_BUF re-grants the same buffer. block_count may be 0 if unknown. */
#define SD_CMD_READ    2 /* req {cmd, lba, count}; data lands at buffer offset 0; reply {Err status} */
#define SD_CMD_WRITE   3 /* req {cmd, lba, count}; data taken from buffer offset 0; reply {Err status} */

/* Block transfer failure (CRC / timeout / FIFO over- or under-run). */
#define SD_ERR_IO ERR_IO

#define SD_BLOCK_SIZE 512u
#define SD_BUF_SIZE 4096u /* one page: SD_BUF_SIZE / SD_BLOCK_SIZE = 8 blocks per request */

typedef struct { uint32_t cmd; uint32_t lba; uint32_t count; } SdRequest;
typedef struct { Err status; uint32_t buf_size; uint32_t block_size; uint32_t block_count; } SdReply;

#ifdef __cplusplus
}
#endif

#endif /* SD_PROTOCOL_H */

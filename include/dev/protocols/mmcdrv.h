#ifndef SD_PROTOCOL_H
#define SD_PROTOCOL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <zuzu/err.h>

/* Generic SD/block-device driver protocol
 * Provides a minimal set of commands for block read/write via a shared buffer.
 */

/* PL181/SD driver commands */
#define SD_CMD_GET_BUF 1
#define SD_CMD_READ    2
#define SD_CMD_WRITE   3

/* Block transfer failure (CRC / timeout / FIFO over- or under-run). Has no
 * Err equivalent, so it lives outside the Err range to avoid collisions. */
#define SD_ERR_IO ERR_IO

/* SD command semantics. Replies use ZUZU_OK on success; failures use Err
 * values from <zuzu/err.h> or SD_ERR_IO for a block transfer error.
 * SD_CMD_GET_BUF (call)
 *   req:  r2=SD_CMD_GET_BUF
 *   resp: r1=ZUZU_OK, r2=shmem_handle (buffer handle)
 *
 * SD_CMD_READ (call)
 *   req:  r2=SD_CMD_READ, r3=block_num
 *   resp: r1=ZUZU_OK or SD_ERR_IO  - result written into shared buffer
 *
 * SD_CMD_WRITE (call)
 *   req:  r2=SD_CMD_WRITE, r3=block_num
 *   resp: r1=ZUZU_OK or SD_ERR_IO  - data read from shared buffer
 */

#ifdef __cplusplus
}
#endif

#endif /* SD_PROTOCOL_H */

#ifndef SD_PROTOCOL_H
#define SD_PROTOCOL_H

/* Generic SD/block-device driver protocol
 * Provides a minimal set of commands for block read/write via a shared buffer.
 */

/* PL181/SD driver commands */
#define SD_CMD_GET_BUF 1
#define SD_CMD_READ    2
#define SD_CMD_WRITE   3

/* SD command semantics:
 * SD_CMD_GET_BUF (call)
 *   req:  r2=SD_CMD_GET_BUF
 *   resp: r1=0 ok, r2=shmem_handle (buffer handle)
 *
 * SD_CMD_READ (call)
 *   req:  r2=SD_CMD_READ, r3=block_num
 *   resp: r1=0 ok, -1 err  - result written into shared buffer
 *
 * SD_CMD_WRITE (call)
 *   req:  r2=SD_CMD_WRITE, r3=block_num
 *   resp: r1=0 ok, -1 err  - data read from shared buffer
 */

#endif /* SD_PROTOCOL_H */

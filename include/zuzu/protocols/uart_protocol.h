#ifndef UART_PROTOCOL_H
#define UART_PROTOCOL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <zuzu/err.h>

/* Generic UART driver protocol (used by any UART driver process)
 * Commands are intentionally generic so user processes can talk
 * to any backend implementing this contract.
 */

/* LMSG-only commands */
#define UART_CMD_WRITE 1
#define UART_CMD_READ 2

/**
 * UART_CMD_WRITE:
 *   r1 = UART_CMD_WRITE
 *   r2 = length of data in LMSG buffer
 *   returns: r0 = 0 (ok) or error
 *
 * UART_CMD_READ:
 *   r1 = UART_CMD_READ
 *   r2 = maximum bytes to read into LMSG buffer
 *   returns: r0 = reply_handle, r1 = bytes_read
 */

#define UART_INIT_OK 0
#define UART_INIT_FAIL ERR_IO

#define UART_SEND_OK 0
#define UART_RECV_OK 0
#define UART_ERR ERR_IO

#ifdef __cplusplus
}
#endif

#endif /* UART_PROTOCOL_H */

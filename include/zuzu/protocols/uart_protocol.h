#ifndef UART_PROTOCOL_H
#define UART_PROTOCOL_H

#include <stdint.h>

/* Generic UART driver protocol (used by any UART driver process)
 * Commands are intentionally generic so user processes can talk
 * to any backend implementing this contract.
 */

/* IPCX-only commands */
#define UART_CMD_WRITE 1
#define UART_CMD_READ 2

/**
 * UART_CMD_WRITE:
 *   r1 = UART_CMD_WRITE
 *   r2 = length of data in IPCX buffer
 *   returns: r0 = 0 (ok) or error
 *
 * UART_CMD_READ:
 *   r1 = UART_CMD_READ
 *   r2 = maximum bytes to read into IPCX buffer
 *   returns: r0 = reply_handle, r1 = bytes_read
 */

#define UART_INIT_OK 0
#define UART_INIT_FAIL -1

#define UART_SEND_OK 0
#define UART_RECV_OK 0
#define UART_ERR -1

#endif /* UART_PROTOCOL_H */

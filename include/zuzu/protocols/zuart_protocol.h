#ifndef ZUART_PROTOCOL_H
#define ZUART_PROTOCOL_H

#include <stdint.h>

// ------------------- IPC constants -------------------

/* IPCX-only commands */
#define ZUART_CMD_WRITE 1
#define ZUART_CMD_READ 2

/**
* IPC contract for ZUART:
*
* === IPCX commands ===
*
* ZUART_CMD_WRITE:
*   r1 = ZUART_CMD_WRITE
*   r2 = length of data in IPCX buffer
*   returns: r0 = 0 (ok) or error
*
* ZUART_CMD_READ:
*   r1 = ZUART_CMD_READ
*   r2 = maximum bytes to read into IPCX buffer
*   returns: r0 = reply_handle, r1 = bytes_read
*/

// -----------------------------------------------------

// ------------------- zuart constants -------------------

#define ZUART_INIT_OK 0
#define ZUART_INIT_FAIL -1

#define ZUART_SEND_OK 0
#define ZUART_RECV_OK 0
#define ZUART_ERR -1

// -----------------------------------------------------

#endif /* ZUART_PROTOCOL_H */
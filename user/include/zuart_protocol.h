#ifndef ZUART_PROTOCOL_H
#define ZUART_PROTOCOL_H

// ------------------- IPC constants -------------------

#define ZUART_CMD_WRITE 1
#define ZUART_CMD_READ 2
/**
* r0 = port handle (used by IPC)
* r1 = ZUART_CMD_*
* r2 = pointer to string
* r3 = length
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
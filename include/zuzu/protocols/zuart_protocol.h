#ifndef ZUART_PROTOCOL_H
#define ZUART_PROTOCOL_H

// ------------------- IPC constants -------------------

#define ZUART_CMD_WRITE 1
#define ZUART_CMD_READ 2

/**
* IPC contract for ZUART:
* * ZUART_CMD_WRITE:
* r0 = port handle (used by IPC)
* r1 = ZUART_CMD_WRITE
* r2 = shmem_handle (containing the string/data to send)
* r3 = length of the data to write
* * ZUART_CMD_READ:
* r0 = port handle (used by IPC)
* r1 = ZUART_CMD_READ
* r2 = shmem_handle (allocated by client, to be filled by zuart)
* r3 = maximum length to read (size of the shared memory buffer)
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
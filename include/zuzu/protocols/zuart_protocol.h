#ifndef ZUART_PROTOCOL_H
#define ZUART_PROTOCOL_H

#include <stdint.h>

// ------------------- IPC constants -------------------

#define ZUART_CMD_WRITE 1
#define ZUART_CMD_READ 2

/*
 * Call-mode payload now carries only one arg beyond command.
 * Pack shmem handle + length into one 32-bit word:
 *   high 16: shmem handle, low 16: requested length
 */
static inline uint32_t zuart_pack_arg(int32_t shmem_handle, uint32_t len)
{
	return (((uint32_t)shmem_handle & 0xFFFFu) << 16) | (len & 0xFFFFu);
}

static inline int32_t zuart_arg_handle(uint32_t packed)
{
	return (int32_t)((packed >> 16) & 0xFFFFu);
}

static inline uint32_t zuart_arg_len(uint32_t packed)
{
	return packed & 0xFFFFu;
}

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
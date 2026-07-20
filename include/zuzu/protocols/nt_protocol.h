#ifndef ZUZU_NT_PROTOCOL_H
#define ZUZU_NT_PROTOCOL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <zuzu/err.h>

#define NT_PORT 0
#define NT_NAME_SYS "sys"

#define NT_REGISTER 1

/**
 * NT_LOOKUP: resolve a registered name to a port.
 *
 * Request  (zuzu_msg_call): w1 = NT_LOOKUP | (den_id << 8), w2 = nt_pack(name)
 * Reply    (zuzu_msg_reply): r1 = NT_LU_OK or NT_LU_NOMATCH
 *                            r2 = port handle, granted into the caller's table
 *                            r3 = pid of the process that registered the name
 *
 * r2/r3 are only meaningful when r1 == NT_LU_OK; both are 0 otherwise.
 * The owner pid in r3 lets a client match later messages against the service
 * it actually resolved, rather than trusting whoever happens to reply.
 */
#define NT_LOOKUP 2
#define DEN_CREATE 3
#define DEN_INVITE 4
#define DEN_KICK 5
#define DEN_MYDEN 6
#define DEN_MYDEN_COUNT 7
#define DEN_MYDEN_AT 8

#include <stdint.h>

/**
 * @brief Packs a 4-character name into a 32-bit unsigned integer.
 * 
 * @param name A 4-character array representing the name to be packed.
 * @return uint32_t The packed 32-bit unsigned integer representation of the name.
 */
static inline uint32_t nt_pack(const char name[4])
{
	return ((uint32_t)(unsigned char)name[0]) |
		   ((uint32_t)(unsigned char)name[1] << 8) |
		   ((uint32_t)(unsigned char)name[2] << 16) |
		   ((uint32_t)(unsigned char)name[3] << 24);
}

#define NT_LU_OK ZUZU_OK
#define NT_LU_NOMATCH ERR_NOSYS
#define NT_REG_FAIL ERR_BUSY
#define NT_REG_OK ZUZU_OK
#define NT_BADCMD ERR_BADARG
#define DEN_OK ZUZU_OK
#define DEN_FAIL ERR_TIMEOUT
#define DEN_FULL ERR_BUSY

#ifdef __cplusplus
}
#endif

#endif
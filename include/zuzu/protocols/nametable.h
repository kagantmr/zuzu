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
 * Request  (ZuzuMsgCall): w1 = NT_LOOKUP | (den_id << 8), w2 = nt_pack(name)
 * Reply    (ZuzuMsgReply): r1 = NT_LU_OK or NT_LU_NOMATCH
 *                            r2 = port handle, granted into the caller's table
 *                            r3 = pid of the process that registered the name
 *
 * r2/r3 are only meaningful when r1 == NT_LU_OK; both are 0 otherwise.
 * The owner pid in r3 lets a client match later messages against the service
 * it actually resolved, rather than trusting whoever happens to reply.
 */
#define NT_LOOKUP 2

/**
 * NT_LOOKUP_PID: resolve a registered pid back to its stored (handle, pid)
 * entry. sysd-internal use only (tty aliasing). unlike NT_LOOKUP, no
 * re-grant is performed, since the returned handle is only ever
 * re-registered under an alias name in nameserver's own table, never held
 * by the caller directly.
 *
 * Request  (ZuzuMsgCall): w1 = NT_LOOKUP_PID, w2 = target pid, w3 = 0
 * Reply    (ZuzuMsgReply): r1 = NT_LU_OK or NT_LU_NOMATCH
 *                            r2 = stored handle (slot in nameserver's table)
 *                            r3 = pid (echoes the input on success)
 */
#define NT_LOOKUP_PID 3

/**
 * NT_SCRUB_PID: notifies nameserver that a process died, so any
 * registrations under that pid are dropped. Fire-and-forget, sent by sysd
 * from its reap loop.
 *
 * Request (ZuzuMsgSend): w1 = NT_SCRUB_PID, w2 = 0 (unused),
 *                          w3 = pid to scrub
 */
#define NT_SCRUB_PID 4

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

#ifdef __cplusplus
}
#endif

#endif

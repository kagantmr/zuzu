#ifndef ZUZU_ERR_H
#define ZUZU_ERR_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef int32_t err_t;

/*
 * These codes are part of the syscall contract: the number
 * and its meaning never change, and no code is ever removed or renumbered.
 * New kernel codes may only be added in the reserved -13..-99 band.
 *
 */
#define ZUZU_OK          0

#define ERR_NOPERM     (-1)   /* caller lacks the right for this operation      */
#define ERR_NOENT      (-2)   /* a name or id (not a handle) does not exist     */
#define ERR_BUSY       (-3)   /* object exists but is in the wrong state/in use */
#define ERR_NOMEM      (-4)   /* kernel resource exhausted: PMM, kmalloc, table, VA */
#define ERR_BADTYPE    (-5)   /* handle exists but is the wrong type here       */
#define ERR_BADARG     (-6)   /* a passed value is invalid (flag, size, count)  */
#define ERR_NOSYS      (-7)   /* no such syscall, or no such RPC operation      */
#define ERR_BADPTR     (-8)   /* user pointer invalid, unmapped, or uncopyable  */
#define ERR_DEAD       (-9)   /* object was destroyed / peer gone               */
#define ERR_TIMEOUT   (-10)   /* deadline expired; also: poll found nothing     */
#define ERR_OVERFLOW  (-11)   /* caller size/payload exceeds a static limit     */
#define ERR_BADHANDLE (-12)   /* handle index names no live entry in the table  */

/* -13 .. -99  reserved for future zuzu kernel codes  */

/*
 * zuzuOS server/library codes (-100 and up). these may be added to or renumbered
 * with the OS. Same rule still holds here: one code, one meaning.
 */
#define ERR_BUFFULL   (-100)  /* ring/channel is full (writer side)             */
#define ERR_BUFEMPTY  (-101)  /* ring/channel is empty (reader side)            */
#define ERR_SYSDOWN   (-102)  /* a required service is not running              */
#define ERR_NOTCONN   (-103)  /* connection/session is not established          */
#define ERR_DUPLICATE (-104)  /* duplicate registration/request rejected        */
#define ERR_MALFORMED (-105)  /* received bytes fail to parse (wire/packet)     */
#define ERR_IO        (-106)  /* device or filesystem I/O error                 */
#define ERR_OUTDATED  (-107)  /* this feature is deprecated or there is a version mismatch between binaries    */

/**
 * Fatal error codes (comes from critical processes and is delivered to the kernel)
 * 
 * Do NOT use these codes as the exit code.
 */
#define FATAL_TAG        0xFA7A0000u
#define FATAL_TAG_MASK   0xFFFF0000u
#define FATAL_REASON_MASK 0x0000FFFFu
#define FATAL_KERNEL_OUTDATED 1
#define FATAL_KERNEL_REFERLMSG 1


/**
 * @brief Returns a human-readable string describing the given error code.
 *
 * @param err The error code to describe.
 * @return const char* A pointer to a string describing the error code.
 */
const char *strtoerror(err_t err);

#ifdef __cplusplus
}
#endif

#endif // ZUZU_ERR_H

#ifndef ZUZU_TYPES_H
#define ZUZU_TYPES_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include "err.h"


typedef int32_t Handle; /* Index into kernel-managed handle table */
typedef int32_t Pid; /* zuzu Process ID or -err */
typedef int32_t Tid; /* zuzu Thread ID or -err */
typedef uint32_t DenID; /* sysd den ID */
typedef uint64_t Tick; /* Monotonic tick counts */
typedef uintptr_t PhysAddr; /* Physical memory address */
typedef uintptr_t VirtAddr; /* Virtual memory address */
typedef uint32_t Irq; /* IRQ number*/
typedef uint64_t Time; /* wall-clock time */

/* ---- Common IPC types ---- */

typedef struct
{
    int32_t r0;
    uint32_t r1;
    uint32_t r2;
    uint32_t r3;
} msg_t;

/* ---- Process spawn types ---- */

typedef struct
{
    Handle task_handle;
    Pid pid;
} tspawn_result_t;

typedef enum {
    WAITANY_KIND_SEND = 0u,
    WAITANY_KIND_CALL = 1u,
    WAITANY_KIND_NTFN = 2u,
    WAITANY_KIND_TIMEOUT = 3u,
} waitany_type_t;

#define WAITANY_NO_MATCH UINT32_MAX

#define TIMEOUT_POLL     0u
#define TIMEOUT_INFINITE UINT32_MAX

#define HANDLE_ANON ((Handle)-1) // Sentinel value, used in memmap() as the handle value

/* waitany result struct */
typedef struct {
    uint32_t size;           /* sizeof(waitany_result_t); caller sets, kernel honors */
    uint32_t matched_index;  /* index into the caller's handle array; WAITANY_NO_MATCH on timeout */
    uint32_t kind;           /* WAITANY_KIND_* */
    uint32_t source;         /* send: sender pid | call: reply handle | ntfn: 0 */
    uint32_t r1;             /* send: payload/lmsg len | call: sender pid | ntfn: bits */
    uint32_t r2;             /* send/call: payload or lmsg length */
    uint32_t r3;             /* send/call: payload */
} waitany_result_t;

#ifdef __cplusplus
}
#endif

#endif // ZUZU_TYPES_H
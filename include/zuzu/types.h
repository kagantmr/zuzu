#ifndef ZUZU_TYPES_H
#define ZUZU_TYPES_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdint.h>
#include <stddef.h>
#include "err.h"

typedef int32_t Handle;     /* Index into kernel-managed handle table */
typedef int32_t Pid;        /* zuzu Process ID or -err */
typedef int32_t Tid;        /* zuzu Thread ID or -err */
typedef uint32_t DenID;     /* sysd den ID */
typedef uint64_t Tick;      /* Monotonic tick counts */
typedef uintptr_t PhysAddr; /* Physical memory address */
typedef uintptr_t VirtAddr; /* Virtual memory address */
typedef uint32_t Irq;       /* IRQ number*/
typedef uint32_t Duration;  /* for sleep and waitany syscalls */
typedef uint64_t Time;      /* wall-clock time */

/* ---- Common IPC types ---- */

typedef int32_t MsgWordSigned;
typedef uint32_t MsgWord;

typedef struct
{
    MsgWordSigned w0;
    MsgWord w1;
    MsgWord w2;
    MsgWord w3;
} Message;

#define TIMEOUT_POLL 0u
#define TIMEOUT_INFINITE UINT32_MAX

/*  Process spawn types  */

typedef struct
{
    Handle taskHandle;
    Pid pid;
} TSpawnResult;

/* Handle sentinels  */

#define HANDLE_ANON ((Handle) - 1) // Sentinel value used in memmap() as the handle value
#define MARKER_NONE 0 /* Means unbadged */

    /* Waitany sentinels, enums and structs */

#define WAITANY_NO_MATCH UINT32_MAX

typedef enum
{
    WAITANY_KIND_SEND = 0u,
    WAITANY_KIND_CALL = 1u,
    WAITANY_KIND_NTFN = 2u,
    WAITANY_KIND_TIMEOUT = 3u,
} WaitanyType;

typedef struct
{
    uint32_t size;        /* sizeof(WaitanyResult); caller sets, kernel honors */
    Handle matched_index; /* index into the caller's handle array; WAITANY_NO_MATCH on timeout */
    WaitanyType kind;     /* WAITANY_KIND_* */
    uint32_t source;      /* send: sender pid | call: reply handle | ntfn: 0 */
    MsgWord w1;           /* send: payload/lmsg len | call: sender pid | ntfn: bits */
    MsgWord w2;           /* send/call: payload or lmsg length */
    MsgWord w3;           /* send/call: payload */
    MsgWord marker;        /* Added in 1.1: waitany on 32-bit architectures is the only syscall that can use markers */
} WaitanyResult;

#ifdef __cplusplus
}
#endif

#endif // ZUZU_TYPES_H
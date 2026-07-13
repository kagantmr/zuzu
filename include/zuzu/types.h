#ifndef ZUZU_TYPES_H
#define ZUZU_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include "err.h"

typedef int32_t handle_t;
typedef uint32_t zpid_t;
typedef uint32_t tid_t;
typedef uint32_t den_id_t;
typedef uint64_t tick_t;
typedef uintptr_t paddr_t;
typedef uintptr_t vaddr_t;
typedef uint32_t irq_t;
typedef uint64_t ztime_t;


/* ---- Common IPC types ---- */

typedef struct
{
    int32_t r0;
    uint32_t r1;
    uint32_t r2;
    uint32_t r3;
} msg_t;

/* ---- Memory management types ---- */

typedef struct
{
    int32_t handle;
    void *addr;
} shmem_result_t;

/* ---- Process spawn types ---- */

typedef struct
{
    handle_t task_handle;
    zpid_t pid;
} tspawn_result_t;

enum {
    RECVANY_KIND_SEND = 0u,
    RECVANY_KIND_CALL = 1u,
    RECVANY_KIND_NTFN = 2u,
    RECVANY_KIND_TIMEOUT = 3u,
};

#define RECVANY_NO_MATCH UINT32_MAX

#define TIMEOUT_POLL     0u
#define TIMEOUT_INFINITE UINT32_MAX

#define HANDLE_ANON ((handle_t)-1) // Sentinel value, used in memmap() as the handle value

/* recvany result struct */
typedef struct {
    uint32_t matched_index;  /* which handle in the input array */
    uint32_t kind;           /* 0=send, 1=call, 2=irq, 3=timeout */
    union {
        zpid_t sender_pid;  /* for send */
        handle_t reply_handle; /* for call */
    };
    uint32_t source;         /* send: sender_pid; call: reply_handle */
    uint32_t r1;             /* send: payload; call: sender_pid */
    uint32_t r2;             /* send/call: payload or IPCX length */
    uint32_t r3;             /* send/call: payload */
} recvany_result_t;

#endif // ZUZU_TYPES_H
#ifndef ZUZU_TYPES_H
#define ZUZU_TYPES_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <stddef.h>
#include <stdint.h>

typedef int32_t Handle;     /* Index into kernel-managed handle table */
typedef int32_t Spid;       /* zuzu Space ID or -err */
typedef int32_t Tid;        /* zuzu Thread ID or -err */
typedef uint64_t Tick;      /* Monotonic tick counts */
typedef uintptr_t PhysAddr; /* Physical memory address */
typedef uintptr_t VirtAddr; /* Virtual memory address */
typedef uint32_t Irq;       /* IRQ number */
typedef uint32_t Duration;  /* for sleep and other timeout-taking syscalls */
typedef uint64_t Time;      /* wall-clock time */
typedef int32_t Err;        /* Error code */
typedef uint32_t Marker;
typedef uint32_t EventWord;

/* ---- Common IPC types ---- */

#define TIMEOUT_POLL 0u
#define TIMEOUT_INFINITE UINT32_MAX

typedef struct
{
    Handle task_handle;
    Spid pid;
} TSpawnResult;

/* Handle sentinels  */

#define HANDLE_ANON ((Handle) - 1) /* Sentinel value used in memmap() as the handle value */
#define MARKER_NONE 0              /* Means unbadged */

/* First handle slot in rootsvc's table where the kernel loader seeds device
 * MemObjects (kernel/loader/boot_programs.c); devsvc indexes from here. */
#define DEVICE_HANDLE_BASE 16

typedef enum
{
    PERM_WAIT = (1U << 0),
    PERM_SEND = (1U << 1),
    PERM_TXFR = (1U << 2),
    PERM_MAP = (1U << 3),
    PERM_CNTL = (1U << 4),
    PERM_ALL = (PERM_WAIT | PERM_SEND | PERM_TXFR | PERM_MAP | PERM_CNTL)
} HandlePerms;

/**
    * @brief Enum for the first argument of Create().
    */
typedef enum
{
    OBJECT_TASK = 0,
    OBJECT_SPACE,
    OBJECT_PORT,
    OBJECT_EVENT,
    OBJECT_MEMORY,
    OBJECT_CODE_COUNT
} ZuzuObjectCode;

/**
    * @brief This struct represents the 4 arguments passed into ManageHandle() to start a task.
    */
typedef struct
{
    VirtAddr entry;
    VirtAddr sp;
    uint32_t r0, r1;
} KickstartArgs;

typedef enum
{
    TASK_EXITED = 0,
    TASK_FAULTED = 1
} TaskWaitOutcome;
typedef struct
{
    Err status;
    Marker sender;
    uint32_t xlen;
    Handle granted;
} PortWaitResult;
typedef struct
{
    Err status;
    EventWord bits;
} EventWaitResult;
typedef struct
{
    Err status;
    TaskWaitOutcome outcome;
    int32_t value;
} TaskWaitResult;
typedef struct
{
    Err status;
    Err exit_status;
} SpaceWaitResult;

/* Kernel event types users can subscribe to */
typedef enum
{
    EVENT_GENERIC = 0, /* no distinction */
    EVENT_MEMMGMT,     /* memory pressure */
    EVENT_IRQ,         /* interrupts */
    EVENT_PORT,        /* port events */
    EVENT_TASK,        /* task events */
    EVENT_SPACE,       /* space events */
    EVENT_TIMER        /* timers */
} EventType;

typedef enum
{
    PROT_NONE = 0,        // no access
    PROT_READ = 1U << 0,  // read access
    PROT_WRITE = 1U << 1, // write access
    PROT_EXEC = 1U << 2   // execute access
} MemProt;

#define PROT_RW ((PROT_READ) | (PROT_WRITE))

typedef enum
{
    MNGMEM_MAP,
    MNGMEM_UNMAP,
    MNGMEM_PROTECT,
    MNGMEM_INJECT,
    MNGMEM_VERB_COUNT
} ManageMemoryVerb;

typedef enum
{
    QUERY_TYPE,
    QUERY_PERMS,
    QUERY_MARKER,
    QUERY_STATUS,
    QUERY_WHAT_COUNT
} QueryWhat;

typedef enum
{
    MNGHNDL_DUPLICATE,
    MNGHNDL_RESTRICT,
    MNGHNDL_CLOSE,
    MNGHNDL_QUERY,
    MNGHNDL_DESTROY,
    MNGHNDL_VERB_COUNT
} ManageHandleVerb;

typedef enum
{
    MNGTASK_START,
    MNGTASK_KILL,
    MNGTASK_SET_PRIORITY,
    MNGTASK_SET_MAX_PRIO,
    MNGTASK_SET_TIMESLICE,
    MNGTASK_SUSPEND,
    MNGTASK_RESUME,
    MNGTASK_GET_REGS,
    MNGTASK_SET_REGS,
    MNGTASK_VERB_COUNT
} ManageTaskVerb;

#define SIGNAL_BROADCAST (1U << 0)

/* AsInjectArgs.flags */
#define ASINJECT_FLAG_RESERVE                                                                      \
0x1U /* reserve [DestVAddr, DestVAddr+len) as demand-zero                                      \
        * anon memory in the target AS; src_buf must                                             \
        * be NULL, no bytes are copied up front. */

typedef struct
{
    uint32_t size;       /* wrapper sets it */
    VirtAddr dest_vaddr; // destination virtual address in the target task's address space
    const void *src_buf; // pointer to the source buffer in the current task's address space
    size_t len;          // length of the source buffer in bytes
    MemProt prot;   // memory protection flags for the destination mapping (e.g., PROT_READ |
                    // PROT_WRITE)
    uint32_t flags; // ASINJECT_FLAG_* bits; 0 for the original copy-in behavior
} InjectArgs;

#define WNOHANG (1 << 0)

#ifdef __cplusplus
}
#endif

#endif // ZUZU_TYPES_H

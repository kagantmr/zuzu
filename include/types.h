#ifndef ZUZU_TYPES_H
#define ZUZU_TYPES_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

typedef int32_t Handle;
typedef int32_t Spid;
typedef int32_t Tid;
typedef uint64_t Tick;
typedef uintptr_t PhysAddr;
typedef uintptr_t VirtAddr;
typedef uint32_t Irq;
typedef uint32_t Duration;
typedef uint64_t Time;
typedef int32_t Err;
typedef uint32_t Marker;
typedef uint32_t Ticket;
typedef uint32_t EventWord;

#define TIMEOUT_POLL 0u
#define TIMEOUT_INFINITE UINT32_MAX

#define TIMESLICE_DEFAULT_US 50000u
#define TIMESLICE_MIN_US 100u
#define TIMESLICE_MAX_US 1000000u

#define HANDLE_ANON ((Handle) - 1)
#define MARKER_NONE 0

#define DEVICE_HANDLE_BASE 16

typedef enum {
    PERM_WAIT = (1U << 0),
    PERM_SEND = (1U << 1),
    PERM_TXFR = (1U << 2),
    PERM_MAP = (1U << 3),
    PERM_CNTL = (1U << 4),
    PERM_ALL = (PERM_WAIT | PERM_SEND | PERM_TXFR | PERM_MAP | PERM_CNTL)
} HandlePerms;

typedef enum {
    OBJECT_TASK = 0,
    OBJECT_SPACE,
    OBJECT_PORT,
    OBJECT_EVENT,
    OBJECT_MEMORY,
    OBJECT_CODE_COUNT
} ZuzuObjectCode;

typedef enum {
    MEM_CONTIG = (1U << 0),
    MEM_UNCACHED = (1U << 1),
    MEM_FLAGS_ALL = (MEM_CONTIG | MEM_UNCACHED)
} CreateMemoryFlags;

typedef enum {
    DMA_TO_DEVICE = 1,
    DMA_FROM_DEVICE = 2,
    DMA_BIDIRECTIONAL = 3,
} DmaDir;

typedef struct {
    Handle dev;
    Handle mem;
    size_t offset;
    size_t len;
    DmaDir dir;
} DmaMapArgs;

typedef struct {
    void *entry;
    void *sp;
    uint32_t r0, r1;
} KickstartArgs;

typedef enum { TASK_EXITED = 0, TASK_FAULTED = 1 } TaskWaitOutcome;
typedef struct {
    Err status;
    Marker sender;
    uint32_t xlen;
    Handle granted;
} PortWaitResult;

typedef struct {
    Err status;
    EventWord bits;
} EventWaitResult;

typedef struct {
    Err status;
    TaskWaitOutcome outcome;
    int32_t value;
} TaskWaitResult;

typedef struct {
    Err status;
    Err exit_status;
} SpaceWaitResult;

typedef struct {
    Err status;
    uintptr_t bus_addr;
    uint32_t id;
} DmaMapResult;

#define DMA_ALIGN 64U

typedef enum { DMA_SYNC_FOR_DEVICE, DMA_SYNC_FOR_CPU } DmaSyncOp;

typedef enum {
    EVENT_MEMMGMT = 1,
    EVENT_IRQ,
    EVENT_PORT,
    EVENT_TASK,
    EVENT_SPACE,
} EventType;

typedef enum {
    PROT_READ = 1U << 0,
    PROT_WRITE = 1U << 1,
    PROT_EXEC = 1U << 2
} MemProt;

#define PROT_RW ((PROT_READ) | (PROT_WRITE))

typedef enum {
    MNGMEM_MAP,
    MNGMEM_UNMAP,
    MNGMEM_PROTECT,
    MNGMEM_INJECTOBJ,
    MNGMEM_DMAMAP,
    MNGMEM_DMAUNMAP,
    MNGMEM_DMASYNC,
    MNGMEM_DMAADOPT,
} ManageMemoryVerb;

typedef enum {
    QUERY_TYPE,
    QUERY_PERMS,
    QUERY_MARKER,
    QUERY_STATUS,
    QUERY_SIZE,
    QUERY_DMA_COUNT,
    QUERY_DMA_ID,
    QUERY_DMA_BUS,
    QUERY_DMA_LEN,
    QUERY_DMA_DIR,
    QUERY_WHAT_COUNT
} QueryWhat;

typedef enum {
    MNGHNDL_DUPLICATE,
    MNGHNDL_RESTRICT,
    MNGHNDL_CLOSE,
    MNGHNDL_QUERY,
    MNGHNDL_DESTROY,
    MNGHNDL_GRANT,
    MNGHNDL_IRQ_REARM,
    MNGHNDL_VERB_COUNT
} ManageHandleVerb;

typedef enum {
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

typedef struct {
    Handle mem;
    VirtAddr dest_vaddr;
    size_t offset;
    size_t len;
    MemProt prot;
} InjectObjArgs;

#ifdef __cplusplus
}
#endif

#endif // ZUZU_TYPES_H

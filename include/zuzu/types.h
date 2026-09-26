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
    } ManageHandleTaskKickstartArgs;

    /* Kernel event types users can subscribe to */
    typedef enum
    {
        EVENT_GENERIC = 0, /* no distinction */
        EVENT_MEMMGMT,     /* memory pressure */
        EVENT_IRQ,         /* interrupts */
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
        MNGMEM_INJECT
    } ManageMemoryVerb;

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

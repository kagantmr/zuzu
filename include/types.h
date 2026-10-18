/**
 * @file types.h
 * @brief Common types for zlibc and the zuzu kernel.
 */

#ifndef ZUZU_TYPES_H
#define ZUZU_TYPES_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

/** @brief Index into the Space's object table. */
typedef int32_t Handle;

/** @brief Space ID. */
typedef int32_t Spid;

/** @brief Task ID. */
typedef int32_t Tid;

/** @brief Tick from the CPU timer. */
typedef uint64_t Tick;

/** @brief Physical address in integer form. */
typedef uintptr_t PhysAddr;

/** @brief Virtual address in integer form. */
typedef uintptr_t VirtAddr;

/** @brief Interrupt number. */
typedef uint32_t Irq;

/** @brief Duration of time (usually in ms). */
typedef uint32_t Duration;

/** @brief Time, measured in ticks. */
typedef uint64_t Time;

/** @brief Non-positive error code. Check zuzu/err.h for error codes. */
typedef int32_t Err;

/** @brief Stamped value of a distinct object. */
typedef uint32_t Marker;

/** @brief Index into a port's held callers queue. */
typedef uint32_t Ticket;

/** @brief Bits delivered on an event wakeup. */
typedef uint32_t EventWord;

/** @brief Pass this sentinel into timeout arguments for the system call to return without blocking.
 */
#define TIMEOUT_POLL 0u

/** @brief Pass this sentinel  into timeout arguments for the system call to block until the request
 * completes. */
#define TIMEOUT_INFINITE UINT32_MAX

/** @brief Default timeslice for a newly created Task. */
#define TIMESLICE_DEFAULT_US 50000u

/** @brief The smallest allowed value for a timeslice for a Task. */
#define TIMESLICE_MIN_US 100u

/** @brief The largest allowed value for a timeslice for a Task. */
#define TIMESLICE_MAX_US 1000000u

/** @brief Pass into MemMap() to map anonymous/implicit memory. */
#define HANDLE_ANON ((Handle) - 1)

/** @brief Sentinel value for an unstamped object's marker. */
#define MARKER_NONE 0

/** @brief Set this flag in Signal() to wake up all callers at once. */
#define SIGNAL_BROADCAST (1U << 0)

/** @brief DMA buffer alignment value for cache alignment. */
#define DMA_ALIGN 64U

/** @brief Protection flag for a read-write memory object. */
#define PROT_RW ((PROT_READ) | (PROT_WRITE))

/** @brief The base value the Device objects are inserted into rootsvc's object table. devsvc
 * shall start checking from this index.
 */
#define DEVICE_HANDLE_BASE 16

/** @brief Permissions enum for an object. */
typedef enum {
    PERM_WAIT = (1U << 0), /**< This handle can be used for a WaitOn(). */
    PERM_SEND = (1U << 1), /**< Object with this handle can be Signal()led or Call()ed. */
    PERM_TXFR = (1U << 2), /**< Object with this handle can be granted to others. */
    PERM_MAP = (1U << 3),  /**< Object with this handle can be mapped into the Space. */
    PERM_CNTL = (1U << 4), /**< Object with this handle's permissions can be altered */
    PERM_ALL = (PERM_WAIT | PERM_SEND | PERM_TXFR | PERM_MAP |
                PERM_CNTL) /**< Object with all permissions. */
} HandlePerms;

/** @brief Type enum for Create() and Query(). */
typedef enum {
    OBJECT_TASK = 0,  /**< This handle names a Task object. */
    OBJECT_SPACE,     /**< This handle names a Space object. */
    OBJECT_PORT,      /**< This handle names a Port object. */
    OBJECT_EVENT,     /**< This handle names an Event object.*/
    OBJECT_MEMORY,    /**< This handle names a Memory object. */
    OBJECT_CODE_COUNT /**< ZuzuObjectCode enum has this many items. */
} ZuzuObjectCode;

/** @brief Flags enum for Create(). */
typedef enum {
    MEM_CONTIG = (1U << 0),   /**< This memory is physically contiguous. */
    MEM_UNCACHED = (1U << 1), /**< This memory is not cacheable. */
    MEM_FLAGS_ALL =
        (MEM_CONTIG | MEM_UNCACHED) /**< Both flags are OR'ed. Useful for DMA buffers. */
} CreateMemoryFlags;

/** @brief Direction enum for DMA memory operations. */
typedef enum {
    DMA_TO_DEVICE = 1,     /**< This Memory is read from a DMAC and sent to the device */
    DMA_FROM_DEVICE = 2,   /**< This Memory is written to by a DMAC and read from the device */
    DMA_BIDIRECTIONAL = 3, /**< This Memory can be read from and written to by a DMAC. */
} DmaDir;

/** @brief Arguments required to map a DMA buffer. */
typedef struct {
    Handle dev; /**< Device handle for the DMA buffer related to the device. */
    Handle
        mem; /**< Memory buffer to turn into a DMA buffer. Should be MEM_CONTIG | MEM_UNCACHED. */
    size_t offset; /**< Byte offset into the memory object where the DMA window is. */
    size_t len;    /**< Size of the DMA window.   */
    DmaDir dir;    /**< Direction of DMA. */
} DmaMapArgs;

/** @brief Arguments required to map a DMA buffer. */
typedef struct {
    void *entry; /**< Entry point for the Task. */
    void *sp;    /**< Stack point for the Task. Must be mapped. */
    uint32_t r0; /**< First  initial register value for the Task. */
    uint32_t r1; /**< Second initial register value for the Task. */
} KickstartArgs;

/** @brief Status code of a task that was being waited on. */
typedef enum {
    TASK_EXITED = 0, /**< The task exited voluntarily. */
    TASK_FAULTED = 1 /**< The task faulted, freezing every other Task in the Space. */
} TaskWaitOutcome;

/** @brief Return values for WaitOn on a port object. */
typedef struct {
    Err status; /**< If the wait succeeded, returns ZUZU_OK. If the request was nonblocking and the
                   wait timed out, it returns ERR_TIMEOUT. */
    Marker sender;  /**< Marker assigned to the sender's port object. */
    uint32_t xlen;  /**< Length of the message. */
    Handle granted; /**< Handle to a granted object in the receiver's table */
} PortWaitResult;

/** @brief Return values for WaitOn on an event object. */
typedef struct {
    Err status; /**< If the wait succeeded, returns ZUZU_OK. If the request was nonblocking and the
                   wait timed out, it returns ERR_TIMEOUT. */
    EventWord bits; /**< The word at the time of wakeup. */
} EventWaitResult;

/** @brief Return values for WaitOn on a task object. */
typedef struct {
    Err status; /**< If the wait succeeded, returns ZUZU_OK. If the request was nonblocking and the
                   wait timed out, it returns ERR_TIMEOUT. */
    TaskWaitOutcome outcome; /**< Why the task stopped. */
    int32_t value;           /**< If exited gracefully, the task's return code. */
} TaskWaitResult;

/** @brief Return values for WaitOn on a space object. */
typedef struct {
    Err status; /**< If the wait succeeded, returns ZUZU_OK. If the request was nonblocking and the
                   wait timed out, it returns ERR_TIMEOUT. */
    Err exit_status; /**< The last task's return code. */
} SpaceWaitResult;

/** @brief Return values for DmaMap. */
typedef struct {
    Err status;         /**< ZUZU_OK on success, negative on failure */
    uintptr_t bus_addr; /**< Address as seen by the device. */
    uint32_t id;        /**< ID of the mapping, for adoption */
} DmaMapResult;

/** @brief DmaSync operation verb. */
typedef enum {
    DMA_SYNC_FOR_DEVICE, /**< Make the device synchronize. */
    DMA_SYNC_FOR_CPU     /**< Make the CPU synchronize. */
} DmaSyncOp;

/** @brief Event type to bind to a bit of an event. */
typedef enum {
    EVENT_MEMMGMT = 1, /**< Physical memory running low */
    EVENT_IRQ,         /**< Hardware interrupt */
    EVENT_PORT,        /**< Call()ed on port, used to multiplex */
    EVENT_TASK,        /**< A task died */
    EVENT_SPACE,       /**< A space is hollowed out. */
} EventType;

/** @brief Permission bits for memory objects. */
typedef enum {
    PROT_READ = 1U << 0,  /**< Read permission */
    PROT_WRITE = 1U << 1, /**< Write permission */
    PROT_EXEC = 1U << 2   /**< Execute permission: cannot be use with Write */
} MemProt;

/** @brief Arguments used to call InjectObj with. */
typedef struct {
    Handle mem;          /**< The object to inject into the kitten Space */
    VirtAddr dest_vaddr; /**< The VA to inject it to. */
    size_t offset;       /**< The offset to write it into. */
    size_t len;          /**<  The length of the object */
    MemProt prot;        /**< The protection flags of the object. */
} InjectObjArgs;

/** @brief ManageMemory operation code. */
typedef enum {
    MNGMEM_MAP,       /**< Map implicit or explicit memory. */
    MNGMEM_UNMAP,     /**< Map implicit or explicit memory. */
    MNGMEM_PROTECT,   /**< Change protection flags */
    MNGMEM_INJECTOBJ, /**< Inject a memory object to a kitten Space. */
    MNGMEM_DMAMAP,    /**< Map DMA memory. */
    MNGMEM_DMAUNMAP,  /**< Unmap DMA memory. */
    MNGMEM_DMASYNC,   /**< Synchronize hardware during a DMA transfer. */
    MNGMEM_DMAADOPT,  /**< Adopt an existing mapping belonging to a device. */
} ManageMemoryVerb;

/** @brief Query operation code. */
typedef enum {
    QUERY_TYPE,      /**< Query the type of object this handle is referring to. */
    QUERY_PERMS,     /**< Query the permissions this object has. */
    QUERY_MARKER,    /**< Query the marker this object has. */
    QUERY_STATUS,    /**< Query the status of this object */
    QUERY_SIZE,      /**< Query the size: memory objects only */
    QUERY_DMA_COUNT, /**< Query the amount of DMA mappings for this device: device mmio objects
                        only */
    QUERY_DMA_ID,    /**< Query the DMA ID for this object: device mmio objects only. */
    QUERY_DMA_BUS,   /**< Query the bus ID for this object. */
    QUERY_DMA_LEN,   /**< Query the DMA length for this object. */
    QUERY_DMA_DIR    /**< Query the DMA direction for this object. */
} QueryWhat;

/** @brief ManageHandle operation code. */
typedef enum {
    MNGHNDL_DUPLICATE, /**< Duplicate this object to a new handle. */
    MNGHNDL_RESTRICT,  /**< Reduce the permissions of this object. */
    MNGHNDL_CLOSE,     /**< Drop the handle to this object, destroy if no one is referencing it. */
    MNGHNDL_QUERY,     /**< Query this object. */
    MNGHNDL_DESTROY,   /**< If owned by the caller, destroy this object fully. */
    MNGHNDL_GRANT,     /**< Grant this object to a kitten. */
    MNGHNDL_IRQ_REARM  /**< Re-arm an interrupt of this device. */
} ManageHandleVerb;

/** @brief ManageTask operation code. */
typedef enum {
    MNGTASK_START,         /**< Start a created task */
    MNGTASK_KILL,          /**< Kill an owned task */
    MNGTASK_SET_PRIORITY,  /**< Change the priority of the caller or an owned task  */
    MNGTASK_SET_MAX_PRIO,  /**< Change the max. priority of the caller or an owned task  */
    MNGTASK_SET_TIMESLICE, /**< Change the timeslice of the caller or an owned task  */
    MNGTASK_SUSPEND,       /**< Suspend an owned task.  */
    MNGTASK_RESUME,        /**< Resume an owned task. */
    MNGTASK_GET_REGS,      /**< Get the register frame of an owned task. */
    MNGTASK_SET_REGS       /**< Change the register frame of an owned task. */
} ManageTaskVerb;

#ifdef __cplusplus
}
#endif

#endif // ZUZU_TYPES_H

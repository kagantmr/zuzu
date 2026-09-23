#ifndef ZUZU_TYPES_H
#define ZUZU_TYPES_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

typedef int32_t Handle;     /* Index into kernel-managed handle table */
typedef int32_t Spid;        /* zuzu Process ID or -err */
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

typedef int32_t MsgWordSigned;
typedef uint32_t MsgWord;

typedef struct {
  MsgWordSigned w0;
  MsgWord w1;
  MsgWord w2;
  MsgWord w3;
} Message;

#define TIMEOUT_POLL 0u
#define TIMEOUT_INFINITE UINT32_MAX

typedef struct {
  Handle task_handle;
  Spid pid;
} TSpawnResult;

/* Handle sentinels  */

#define HANDLE_ANON  ((Handle) -1)      /* Sentinel value used in memmap() as the handle value */
#define MARKER_NONE 0 /* Means unbadged */
#define LABEL_NONE 0  /* Means init hasnt set a label */

/**
 * @brief Enum for the first argument of Create().
 */
typedef enum {
    CREATE_TASK = 0, /**< Indicates that the following arguments of Create should belong to a Task */
    CREATE_SPACE,   /**< Indicates that the following arguments of Create should belong to a Space */
    CREATE_PORT,    /**< Indicates that the following arguments of Create should belong to a Port */
    CREATE_EVENT,   /**< Indicates that the following arguments of Create should belong to an Event */
    CREATE_MEMORY,  /**< Indicates that the following arguments of Create should belong to a Memory object */
    CREATE_TYPES_COUNT
} CreateType;

/**
 * @brief This struct represents the 4 arguments passed into ManageHandle() to start a task.
 */
typedef struct {
    VirtAddr entry;
    VirtAddr sp;
    uint32_t r0, r1;
} ManageHandleTaskKickstartArgs;

/* Kernel event types users can subscribe to */
typedef enum {
    KEVENT_MEMMGMT = 0,  /* memory pressure */
} KEventType;

/* ---- Process constants ---- */

#define WNOHANG (1 << 0)

#ifdef __cplusplus
}
#endif

#endif // ZUZU_TYPES_H

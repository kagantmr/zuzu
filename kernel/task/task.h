/**
 * @file task.h
 * @brief Tasks are schedulable contexts of execution that can share a space.
 */

#ifndef KERNEL_TASK_TASK_H
#define KERNEL_TASK_TASK_H

#include "kernel/ipc/event.h"
#include "kernel/ipc/observer.h"
#include "kernel/ipc/port.h"
#include "kernel/mm/vmm/vmm.h"
#include <arch/fpu.h>
#include <arch/regs.h>
#include <list.h>
#include <types.h>

/**
 * @brief Forward declaration of SpaceObject.
 */
typedef struct SpaceObjectStruct SpaceObject;

/**
 * @brief A priority is an integer denoting how much this task is allowed to use the CPU
 */
typedef uint32_t Prio;

/**
 * @brief Represents the task lifecycle.
 */
typedef enum TaskStateEnum {
    TASK_STATE_READY = 0, /**< Task is ready to be scheduled. */
    TASK_STATE_RUNNING,   /**< Task is already on the CPU. */
    TASK_STATE_BLOCKED,   /**< Task is waiting on an object. */
    TASK_STATE_ZOMBIE,    /**< Task has exited, it is waiting for reaping. */
    TASK_STATE_FROZEN,    /**< Task has been suspended by its parent. */
    TASK_STATE_FAULTED,   /**< Task has raised a hardware exception. */
} TaskState;

/**
 * @brief Describes the IPC state of a task.
 */
typedef enum IpcStateEnum {
    IPC_NONE = 0, /**< Not waiting on a port. */
    IPC_WAITING,  /**< Waiting on a port. */
} IpcState;

/**
 * @brief TaskObjectStruct typedef.
 */
typedef struct TaskObjectStruct TaskObject;

/**
 * @brief Sentinel byte for no TCB slot.
 */
#define TCB_SLOT_NONE 0xFFu

/**
 * @brief Represents an entry in a waiter queue.
 */
typedef struct WaitSlotStruct {
    ListNode node;     /**< The queue node. */
    TaskObject *owner; /**< The waiting task. */
    ListHead *queue;   /**< Pointer to the head of this waiter queue. */
} WaitSlot;

/**
 * @brief Represents a task object.
 */
struct TaskObjectStruct {
    VirtAddr kernel_stack_top; /**< Top of the kernel stack. */
    CpuState *trap_frame;      /**< Pointer to allocated trap frame */
    Tid tid;                   /**< Task ID. */
    void *kernel_sp;           /**< Saved kernel stack pointer while the task is switched out. */
    Err exit_status;           /**< If exited, saved status. */
    ListNode node;            /**< Run queue or port sender queue link, whichever the task is on. */
    ListNode space_node;      /**< Owner space node. */
    ListNode timeout_node;    /**< Sleep wheel link while the task has a timeout. */
    ListHead joiners;         /**< Tasks waiting for this task to exit. */
    Time wake_deadline;       /**< Counter value at which a timeout fires, or 0 for none. */
    Time slice_remaining;     /**< Counter ticks left of the time slice if it was preempted. */
    int16_t sleep_slot;       /**< Sleep wheel slot the task is in, or -1. */
    TaskState state;          /**< Where the task is in its lifecycle. */
    ListNode destroy_node;    /**< Link in the queue of tasks waiting to be reaped. */
    IpcState ipc_state;       /**< Whether the task is waiting on a call. */
    PortObject *blocked_port; /**< The port the task's call is queued or being served on. */
    ReplyObject reply_cap_storage; /**< The reply capability for the task's own outgoing call. */
    Handle pending_grant_handle; /**< Handle a queued call carries, granted when it is received. */
    ReplyObject *pending_reply_cap; /**< Reply capability of the call the task is waiting on. */
    ReplyObject *reply_cap;         /**< The call this task is serving, which it must answer. */
    TaskObject *reply_holder;       /**< The server holding this task's reply capability. */
    PhysAddr msg_buf_phys_addr;     /**< Physical address of the task's message buffer. */
    size_t msg_xfer_len;            /**< Length of the message a queued call carries. */
    Marker port_marker;             /**< Marker of the port handle the task last called through. */
    WaitSlot wait_slot;             /**< Entry in the wait queue the task blocks on. */
    Prio priority;            /**< Effective priority, including any inherited from callers. */
    Prio base_prio;           /**< Priority the task asked for, before inheritance. */
    ListNode serve_node;      /**< Link in the active servers of the port being served. */
    PortObject *serving_port; /**< The port whose call this task is serving, or NULL. */
    uint32_t time_slice_us;   /**< Length of the task's time slice in microseconds. */
    Prio max_prio;            /**< Highest priority the task may set. */
    uint8_t queued_prio;      /**< Run queue level the node is linked at; valid while linked. */
    Time slice_deadline;      /**< Counter value at which the running slice ends. */
    SpaceObject *owner;       /**< The space the task belongs to; NULL once destroyed. */
    VirtAddr task_info_va;    /**< User address of the task's thread-local data. */
    Err fault_reason;         /**< Why the task faulted, if it did. */
    uint8_t tcb_slot;         /**< Slot of the task's thread-local data, or TCB_SLOT_NONE. */
    FpuState fpu_state;       /**< Saved floating point registers. */
    ObserverSet observers;    /**< Events signalled when the task exits or faults. */
    size_t ref_count;         /**< References from handles and other holders. */
    bool released;            /**< TaskDestroy() has run. */
    bool suspended;           /**< Suspended through its handle; the scheduler will not run it. */
    PortObject *held_port;    /**< The port holding this task's call after a park, or NULL. */
    uint8_t held_slot;        /**< Slot in @c held_port's table. */
    Handle serving_handle;    /**< Handle of the port this task last received on. */
    EventWord wait_mask; /**< Bits this task is waiting for on an event. */
    bool wait_all;       /**< Wake only when every bit in wait_mask is pending. */
};

/**
 * @brief Remove a task from every queue, free its kernel stack and thread-local slot, and free it
 * unless something still references it.
 *
 * Calling it twice is harmless.
 *
 * @param[in] task The task to destroy.
 */
void TaskDestroy(TaskObject *task);

/**
 * @brief Create a task in a space.
 *
 * The task starts frozen: it is not scheduled until it is started. It gets a kernel stack, a
 * task ID and a thread-local slot with the default priority and time slice.
 *
 * @param[in] owner The space the task will belong to.
 * @return The task, or NULL if kernel stacks, task IDs, thread-local slots or memory ran out.
 */
TaskObject *TaskCreate(SpaceObject *owner);

/**
 * @brief Wait for a task to exit or fault.
 *
 * Replies at once if it already has, with its status in r1 and the outcome in r3; otherwise
 * blocks the caller. Fails with ERR_BADARG when a task waits on itself.
 *
 * @param[in] task The task to wait on.
 * @param[in] timeout Milliseconds to wait, or TIMEOUT_POLL / TIMEOUT_INFINITE.
 * @param[in] frame The caller's trap frame, which receives the result.
 */
void TaskWaitExit(TaskObject *task, Duration timeout, CpuState *frame);

/**
 * @brief Wake every task on a wait list, telling each that the thing it waited on exited.
 *
 * @param[in] list The list of wait slots.
 * @param[in] status The exit status to hand to each waiter.
 */
void WakeWaitList(ListHead *list, Err status);

/**
 * @brief Take a task off the run queue, the sleep wheel and any wait queue it is in.
 *
 * @param[in] t The task.
 */
void TaskUnlinkWaits(TaskObject *t);

/**
 * @brief Wake a blocked task with an error as the result of its syscall.
 *
 * @param[in] t The blocked task.
 * @param[in] err The error to return to it.
 */
void TaskAbortWait(TaskObject *t, Err err);

/**
 * @brief Withdraw a task's call: remove it from the port's queue or table and clear the
 * server's reply capability, so a late reply cannot reach a later call.
 *
 * @param[in] t The calling task.
 */
void TaskCancelCall(TaskObject *t);

/**
 * @brief Take a reference on a task.
 *
 * @param[in] t The task.
 */
void TaskRef(TaskObject *t);

/**
 * @brief Drop a reference on a task, freeing it if that was the last one and it was released.
 *
 * @param[in] t The task.
 */
void TaskUnref(TaskObject *t);

/**
 * @brief Recompute a task's effective priority from its own and from the callers it serves.
 *
 * Priority inheritance: the task runs at least as high as the caller it is answering and the
 * first caller queued on its port, and the change propagates up to a bounded depth.
 *
 * @param[in] t The task.
 */
void TaskRecomputePriority(TaskObject *t);

/**
 * @brief Stop a task counting as an active server of its port.
 *
 * @param[in] t The task.
 */
void TaskDetachServing(TaskObject *t);

/**
 * @brief Forget the call a task was serving and recompute its priority.
 *
 * @param[in] t The task.
 */
void TaskClearReplyCap(TaskObject *t);

/**
 * @brief Recompute the priority of every task currently serving a port.
 *
 * @param[in] port The port.
 */
void PortBoostServers(PortObject *port);

/**
 * @brief Copy a task's priority and ceiling into its user-visible thread-local data.
 *
 * @param[in] t The task.
 */
void TaskSyncTcbPrio(TaskObject *t);

/**
 * @brief Make a task exit: release what it holds, wake its waiters, and destroy it.
 *
 * The destruction is deferred for the running task and skipped while the space still has other
 * live tasks to report on.
 *
 * @param[in] task The task.
 * @param[in] exit_status The status reported to waiters.
 */
void TaskTerminate(TaskObject *task, Err exit_status);

/**
 * @brief Check whether a task has exited or faulted.
 *
 * @param[in] task The task.
 * @return true if it is a zombie or faulted.
 */
bool TaskIsDead(const TaskObject *task);

/**
 * @brief Mark a task as faulted and freeze its space.
 *
 * Wakes the task's waiters with the fault reason and takes the space's other ready tasks off the
 * run queue until the space is unfrozen.
 *
 * @param[in] task The faulting task.
 * @param[in] reason Why it faulted.
 */
void TaskFault(TaskObject *task, Err reason);
#endif // KERNEL_TASK_TASK_H

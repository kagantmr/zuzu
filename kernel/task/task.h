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

typedef struct SpaceObjectStruct SpaceObject;

typedef enum TaskStateEnum
{
    TASK_STATE_READY = 0, // ready to run, in run queue
    TASK_STATE_RUNNING,   // on CPU
    TASK_STATE_BLOCKED,   // waiting for IPC or timeout
    TASK_STATE_ZOMBIE,    // exited (Quit or killed), not yet reaped
    TASK_STATE_FROZEN,    // not runnable yet
    TASK_STATE_FAULTED, // ran into an exception, or parent/owner stopped it
} TaskState;

typedef enum IpcStateEnum
{
    IPC_NONE = 0,
    IPC_WAITING,
} IpcState;

typedef struct TaskObjectStruct TaskObject;

#define TCB_SLOT_NONE 0xFFu /* task holds no TCB slot */

typedef struct WaitSlotStruct
{
    ListNode node;     /**< Node in the wait queue. */
    TaskObject *owner; /**< Owner task of this wait slot. */
} WaitSlot;

struct TaskObjectStruct
{
    VirtAddr kernel_stack_top; /**< Top of the kernel stack for freeing. */
    CpuState *trap_frame;     /**< Pointer to saved user registers for IPC and context switching. */
    Tid tid;                  /**< Task ID. */
    uint32_t *kernel_sp;      /**< Current kernel stack pointer for context switching. */
    Err exit_status;          /**< Exit status of the task. */
    ListNode node;            /**< Embedded, not pointers. */
    ListNode space_node;      /**< Membership in owner space task list. */
    ListNode timeout_node;    /**< Node for timeout queue. */
    ListHead joiners;         /**< List of joiners. */
    Time wake_deadline;       /**< Deadline for waking up. */
    int16_t sleep_slot;       /**< Sleep slot. */
    TaskState state;          /**< State of the task. */
    ListNode destroy_node;    /**< Node for destruction. */
    IpcState ipc_state;       /**< IPC state. */
    PortObject *blocked_port; /**< Blocked port. */
    ReplyObject reply_cap_storage;
    Handle pending_grant_handle; /**< Waiting for reply. */
    ReplyObject
        *pending_reply_cap; /**< Set while this task is a blocked caller, waiting for its reply. */
    ReplyObject
        *reply_cap;           /**< Set while this task is a receiver mid-call, waiting to Reply. */
    TaskObject *reply_holder; /**< Server currently holding this task's reply cap, or NULL. */
    PhysAddr msg_buf_phys_addr; /**< Physical address of the message buffer. */
    size_t msg_xfer_len;        /**< Length of the message buffer transfer. */
    Marker port_marker;         /**< Port marker. */
    WaitSlot wait_slot;         /**< Wait slot. */
    uint32_t priority, time_slice; /**< Priority and time slice. */
    uint32_t max_prio;
    uint8_t queued_prio;   /**< Run-queue level the node is linked at; valid while node is linked. */
    Time slice_deadline;   /**< Deadline for the time slice. */
    SpaceObject *owner;    /**< Backpointer to the owning Space. */
    VirtAddr task_info_va; /**< User VA of the task's TCB slot. */
    Err fault_reason;
    uint8_t tcb_slot;      /**< Index into owner's TCB page, TCB_SLOT_NONE if unassigned. */
    FpuState fpu_state;    /**< Lazily saved/restored, see kernel/sched/sched.c fpu_owner. */
    ObserverSet observers;
    uint32_t ref_count;
    bool released;
};

_Static_assert(offsetof(TaskObject, kernel_sp) == 12,
               "switch.S expects task->kernel_sp at offset 12");

void TaskDestroy(TaskObject *task);
TaskObject *TaskCreate(SpaceObject *owner);
void TaskWaitExit(TaskObject *task, Duration timeout, CpuState *frame);
void WakeWaitList(ListHead *list, Err status);
void TaskUnlinkWaits(TaskObject *t);
void TaskAbortWait(TaskObject *t, Err err);
void TaskRef(TaskObject *t);
void TaskUnref(TaskObject *t);

/**
 * @brief Unify self-directed Quit and external Term: mark the task TASK_STATE_ZOMBIE,
 * wake any already-blocked joiners, and if it was the last task in its
 * space, tear that space down as a consequence (see SpaceDestroy).
 */
void TaskTerminate(TaskObject *task, Err exit_status);
/* Exited or faulted: the condition observers wait for. */
bool TaskIsDead(const TaskObject *task);
void TaskFault(TaskObject *task, Err reason);
#endif // KERNEL_TASK_TASK_H

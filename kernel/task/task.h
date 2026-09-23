#ifndef ZUZU_THREAD_H
#define ZUZU_THREAD_H

#include "kernel/ipc/event.h"
#include "kernel/ipc/port.h"
#include "kernel/mm/vmm.h"
#include <arch/fpu.h>
#include <arch/regs.h>
#include <list.h>
#include <zuzu/types.h>

typedef struct SpaceObjectStruct SpaceObject;

typedef enum TaskStateEnum
{
    READY = 0, // ready to run, in run queue
    RUNNING,   // on CPU
    BLOCKED,   // waiting for IPC or timeout
    ZOMBIE,    // called quit()
    FROZEN,    // not runnable yet
} TaskState;

typedef enum
{
    WAKE_NONE = 0, // not currently sleeping/waiting
    WAKE_IPC,      // woken by IPC partner
    WAKE_TIMEOUT,  // woken by timer
} WakeReason;

typedef enum MsgStateEnum
{
    IPC_NONE = 0,
    IPC_SENDER,
    IPC_RECEIVER,
    IPC_WAITING,
} MsgState;

typedef struct TaskObjectStruct TaskObject;

#define TCB_SLOT_NONE 0xFFu /* thread holds no TCB slot */

/**
 * @brief A registration linked into one ntfn's wait_queue or one port's
 * receiver_queue for a thread blocked in a plain single-handle wait
 * (ntfn_wait_slot, port_wait_slot below).
 */
typedef struct wait_slot
{
    ListNode node;     /**< Node in the wait queue. */
    TaskObject *owner; /**< Owner task of this wait slot. */
} WaitSlot;

struct TaskObjectStruct
{
    VirtAddr kernel_stack_top; /**< Top of the kernel stack for freeing. */
    CpuState *trap_frame;     /**< Pointer to saved user registers for IPC and context switching. */
    Tid tid;                  /**< Thread ID. */
    uint32_t *kernel_sp;      /**< Current kernel stack pointer for context switching. */
    Err exit_status;          /**< Exit status of the thread. */
    ListNode node;            /**< Embedded, not pointers. */
    ListNode process_node;    /**< Membership in owner process thread list. */
    ListNode timeout_node;    /**< Node for timeout queue. */
    ListHead joiners;         /**< List of joiners. */
    ListNode join_node;       /**< Node for joiners. */
    WakeReason wake_reason;   /**< Reason for waking up. */
    Time wake_deadline;       /**< Deadline for waking up. */
    int16_t sleep_slot;       /**< Sleep slot. */
    TaskState state;          /**< State of the thread. */
    ListNode destroy_node;    /**< Node for destruction. */
    MsgState ipc_state;       /**< IPC state. */
    PortObject *blocked_port; /**< Blocked port. */
    ReplyCap *pending_reply_cap; /**< Pending reply capability. */
    PhysAddr lmsg_buf_phys_addr; /**< Physical address of the message buffer. */
    size_t lmsg_buf_xfer_len;    /**< Length of the message buffer transfer. */
    Marker port_marker;          /**< Port marker. */
    WaitSlot ntfn_wait_slot;     /**< Wait slot for SysNtfnWait. */
    WaitSlot port_wait_slot;     /**< Wait slot for SysMsgRecv. */
    uint32_t priority, time_slice,
        ticks_remaining;   /**< Priority, time slice, and remaining ticks. */
    Time slice_deadline;   /**< Deadline for the time slice. */
    SpaceObject *owner;    /**< Backpointer to owning process. */
    VirtAddr task_info_va; /**< Virtual address of thread info. */
    uint8_t tcb_slot;      /**< Index into owner's TCB page, TCB_SLOT_NONE if unassigned. */
    FpuState fpu_state;    /**< Lazily saved/restored, see kernel/sched/sched.c fpu_owner. */
#ifdef CONFIG_ZUZU_BENCH
    uint32_t bench_irq_wait_start; /**< PMCCNTR at SysNtfnWait block, for the IRQ-wait bench. */
#endif
};

_Static_assert(offsetof(TaskObject, kernel_sp) == 12,
               "switch.S expects process->kernel_sp at offset 12");

void TaskDestroy(TaskObject *task);
TaskObject *TaskCreate(SpaceObject *owner);
void KillTask(TaskObject *task);
void WakeJoinTask(TaskObject *task, Err exit_status);
TaskObject *FindTaskByTid(Tid tid);
void ThreadUnlinkWaits(TaskObject *t);

/**
 * @brief Unify self-directed Quit and external Term: mark the task ZOMBIE,
 * wake any already-blocked joiners, and if it was the last task in its
 * space, tear that space down as a consequence (see SpaceDestroy).
 */
void TaskTerminate(TaskObject *task, Err exit_status);

#endif // ZUZU_THREAD_H

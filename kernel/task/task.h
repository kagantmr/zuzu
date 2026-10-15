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
typedef uint32_t Prio;

typedef enum TaskStateEnum {
    TASK_STATE_READY = 0,
    TASK_STATE_RUNNING,
    TASK_STATE_BLOCKED,
    TASK_STATE_ZOMBIE,
    TASK_STATE_FROZEN,
    TASK_STATE_FAULTED,
} TaskState;

typedef enum IpcStateEnum {
    IPC_NONE = 0,
    IPC_WAITING,
} IpcState;

typedef struct TaskObjectStruct TaskObject;

#define TCB_SLOT_NONE 0xFFu

typedef struct WaitSlotStruct {
    ListNode node;
    TaskObject *owner;
    ListHead *queue;
} WaitSlot;

struct TaskObjectStruct {
    VirtAddr kernel_stack_top;
    CpuState *trap_frame;
    Tid tid;
    void *kernel_sp;
    Err exit_status;
    ListNode node;
    ListNode space_node;
    ListNode timeout_node;
    ListHead joiners;
    Time wake_deadline;
    Time slice_remaining;
    int16_t sleep_slot;
    TaskState state;
    ListNode destroy_node;
    IpcState ipc_state;
    PortObject *blocked_port;
    ReplyObject reply_cap_storage;
    Handle pending_grant_handle;
    ReplyObject
        *pending_reply_cap;
    ReplyObject *reply_cap;
    TaskObject *reply_holder;
    PhysAddr msg_buf_phys_addr;
    size_t msg_xfer_len;
    Marker port_marker;
    WaitSlot wait_slot;
    Prio priority;
    Prio base_prio;
    ListNode serve_node;
    PortObject *serving_port;
    uint32_t time_slice_us;
    Prio max_prio;
    uint8_t queued_prio;
    Time slice_deadline;
    SpaceObject *owner;
    VirtAddr task_info_va;
    Err fault_reason;
    uint8_t tcb_slot;
    FpuState fpu_state;
    ObserverSet observers;
    size_t ref_count;
    bool released;
    bool suspended;  
    PortObject *held_port;
    uint8_t held_slot;
    Handle serving_handle;
};

void TaskDestroy(TaskObject *task);
TaskObject *TaskCreate(SpaceObject *owner);
void TaskWaitExit(TaskObject *task, Duration timeout, CpuState *frame);
void WakeWaitList(ListHead *list, Err status);
void TaskUnlinkWaits(TaskObject *t);
void TaskAbortWait(TaskObject *t, Err err);
void TaskRef(TaskObject *t);
void TaskUnref(TaskObject *t);
void TaskRecomputePriority(TaskObject *t);
void TaskDetachServing(TaskObject *t);
void TaskClearReplyCap(TaskObject *t);
void PortBoostServers(PortObject *port);

void TaskSyncTcbPrio(TaskObject *t);

void TaskTerminate(TaskObject *task, Err exit_status);
bool TaskIsDead(const TaskObject *task);
void TaskFault(TaskObject *task, Err reason);
#endif // KERNEL_TASK_TASK_H

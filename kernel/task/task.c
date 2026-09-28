#include "core/panic.h"
#include "kernel/ipc/msg.h"
#include "kernel/mm/alloc.h"
#include "kernel/sched/sched.h"
#include "kernel/space/space.h"
#include "kstack.h"
#include <spinlock.h>
#include <stddef.h>
#include <string.h>
#include <zuzu/err.h>

#define MAX_THREADS 1024

#define LOG_FMT(fmt) "(task) " fmt
#include "core/ensure.h"
#include <util/log.h>

static Tid next_tid = 1;
static TaskObject *task_table[MAX_THREADS];
static KHeapSlabCache task_cache;

static Tid RegisterTask(TaskObject *task)
{
    if (!task)
        return 0;

    /* Advance next_tid until its hashed slot is free, so the assigned tid
     * always satisfies tid % MAX_THREADS == slot. TaskObjectFindByTid and
     * task_unregister rely on that to stay O(1). Mirrors process_table. */
    Tid start = next_tid % MAX_THREADS;
    Tid slot = start;
    while (task_table[slot] != NULL)
    {
        next_tid++;
        slot = next_tid % MAX_THREADS;
        if (slot == start)
        {
            return 0;
        }
    }

    task->tid = next_tid++;
    task_table[slot] = task;

    KTRACE("task register: tid=%u slot=%d owner_pid=%u owner_name=%s", task->tid, slot,
           (task->owner_process ? task->owner_process->pid : 0),
           (task->owner_process ? task->owner_process->name : "<none>"));

    return task->tid;
}

static void TaskObjectUnregister(TaskObject *task)
{
    if (!task || task->tid == 0)
        return;

    uint32_t slot = (uint32_t)task->tid % MAX_THREADS;
    if (task_table[slot] == task)
        task_table[slot] = NULL;
}

void KillTask(TaskObject *task)
{
    if (!task)
        return;

    task->state = ZOMBIE;
}

void WakeWaitList(ListHead *list, Err status)
{
    while (!list_empty(list))
    {
        ListNode *node = list_pop_front(list);
        if (!node)
            break;
        TaskObject *waiter = container_of(node, WaitSlot, node)->owner;
        if (waiter->trap_frame)
        {
            ArchSetInFrame(waiter->trap_frame, 0, ZUZU_OK);
            (*ArchGetFromFrame(waiter->trap_frame, 1)) = (Register)status;
        }
        SchedUnblock(waiter, WAKE_IPC);
        SchedAdd(waiter);
    }
}

void WakeJoinTask(TaskObject *task, Err exit_status)
{
    if (!task)
        return;

    WakeWaitList(&task->joiners, exit_status);
}

void TaskRef(TaskObject *t)
{
    if (!t)
        return;
    t->ref_count++;
}

void TaskUnref(TaskObject *t)
{
    if (!t)
        return;
    if (t->ref_count > 0)
        t->ref_count--;
    if (t->ref_count == 0 && t->released)
        KSlabFree(&task_cache, t);
}

void TaskDestroy(TaskObject *task)
{
    if (!task || task->released)
        return;
    TaskUnlinkWaits(task);
    TaskObjectUnregister(task);
    if (fpu_owner == task)
        fpu_owner = NULL;
    // may already be removed by tquit, guard is safe
    if (task->space_node.prev && task->space_node.next)
        list_remove(&task->space_node);
    SpaceObject *owner = task->owner;
    if (owner && task->state != ZOMBIE)
        owner->live_tasks--;
    /* Release the TCB slot; scrub it so a reused slot never shows a
     * previous task's tid/pid. tcb_page_pa == 0 means the page is
     * already gone (process teardown fail paths). */
    if (owner && task->tcb_slot < TCB_MAX_SLOTS &&
        owner->tcb_page_pa[task->tcb_slot / SLOTS_PER_PAGE])
    {
        memset((void *)TcbSlotKVirtAddr(owner, task->tcb_slot), 0, TCB_SLOT_SIZE);
        TcbSlotFree(owner, task->tcb_slot);
    }
    if (owner && owner->main_task == task)
        owner->main_task = NULL;
    if (task->kernel_stack_top)
        KernelStackFree(task->kernel_stack_top);

    if (owner && owner->torn_down && list_empty(&owner->tasks))
        SpaceFinalize(owner);

    task->released = true;
    task->owner = NULL;
    if (task->ref_count == 0)
        KSlabFree(&task_cache, task);
}

void TaskWaitExit(TaskObject *task, Duration timeout, CpuState *frame)
{
    ENSURE_ERR(frame, task != current_task, ERR_BADARG);
    if (task->state == ZOMBIE)
    {
        ArchSetInFrame(frame, 0, ZUZU_OK);
        ArchSetInFrame(frame, 1, task->exit_status);
        return;
    }
    if (task->state == FAULTED)
    {
        ArchSetInFrame(frame, 0, ZUZU_OK);
        ArchSetInFrame(frame, 1, WAKE_FAULT);
        ArchSetInFrame(frame, 2, task->fault_reason);
        ArchSetInFrame(frame, 3, 0);
        return;
    }
    SchedBlockOn(&task->joiners, timeout);
}

TaskObject *TaskCreate(SpaceObject *owner)
{
    if (!owner)
        return NULL;

    if (!task_cache.obj_size)
        KSlabInit(&task_cache, "TaskObject", sizeof(TaskObject));
    TaskObject *task = KSlabAlloc(&task_cache);
    if (!task)
        return NULL;
    memset(task, 0, sizeof(*task));

    task->kernel_stack_top = KernelStackAlloc();
    if (!task->kernel_stack_top)
    {
        KSlabFree(&task_cache, task);
        return NULL;
    }

    task->tid = RegisterTask(task);
    if (task->tid == 0)
    {
        KernelStackFree(task->kernel_stack_top);
        KSlabFree(&task_cache, task);
        return NULL;
    }

    task->kernel_sp = NULL;
    task->trap_frame = NULL;
    task->owner = owner;
    task->exit_status = 0;
    task->node.next = NULL;
    task->node.prev = NULL;
    task->sleep_slot = -1;
    task->space_node.next = NULL;
    task->space_node.prev = NULL;
    task->timeout_node.next = NULL;
    task->timeout_node.prev = NULL;
    list_init(&task->joiners);
    task->wait_slot.node.next = NULL;
    task->wait_slot.node.prev = NULL;
    task->wake_reason = WAKE_NONE;
    task->wake_deadline = 0;
    task->state = FROZEN;
    task->ipc_state = IPC_NONE;
    task->blocked_port = NULL;
    task->pending_reply_cap = NULL;
    task->msg_buf_phys_addr = 0;
    task->msg_xfer_len = 0;
    task->priority = SCHED_PRIO_DEFAULT;
    task->time_slice = 5;
    task->max_prio = SCHED_PRIORITY_LEVELS - 1;
    task->ticks_remaining = task->time_slice;
    task->slice_deadline = 0;
    task->task_info_va = 0;
    task->tcb_slot = TCB_SLOT_NONE;

    int tcb_slot_idx = TcbSlotAlloc(owner);
    if (tcb_slot_idx < 0)
    {
        TaskObjectUnregister(task);
        KernelStackFree(task->kernel_stack_top);
        KSlabFree(&task_cache, task);
        return NULL;
    }
    ThreadLocalData *tcb = (ThreadLocalData *)TcbSlotKVirtAddr(owner, (uint32_t)tcb_slot_idx);
    VirtAddr tcb_va = TcbSlotUVirtAddr(owner, (uint32_t)tcb_slot_idx);
    memset(tcb, 0, TCB_SLOT_SIZE);
    tcb->lmsg_buf = (void *)(tcb_va + offsetof(ThreadLocalData, buf));
    tcb->tid = task->tid;
    tcb->spid = owner->spid;
    task->task_info_va = tcb_va;
    task->tcb_slot = (uint8_t)tcb_slot_idx;
    task->msg_buf_phys_addr =
        TcbSlotPhysAddr(owner, (uint32_t)tcb_slot_idx) + offsetof(ThreadLocalData, buf);

    list_add_tail(&task->space_node, &owner->tasks.node);
    owner->live_tasks++;

    if (!owner->main_task)
        owner->main_task = task;

    KTRACE("task create: tid=%u owner_pid=%u owner_name=%s state=%u kernel_stack_top=%p", task->tid,
           owner->pid, owner->name, task->state, (void *)task->kernel_stack_top);

    return task;
}

TaskObject *FindTaskByTid(Tid tid)
{
    if (tid == 0)
        return NULL;

    uint32_t slot = (uint32_t)tid % MAX_THREADS;
    TaskObject *t = task_table[slot];
    if (t && t->tid == tid)
        return t;
    return NULL;
}

void TaskUnlinkWaits(TaskObject *t)
{
    if (!t)
        return;
    if (t->node.prev && t->node.next)
        list_remove(&t->node);
    SchedRemoveSleepQueue(t);
    if (t->wait_slot.node.prev && t->wait_slot.node.next)
        list_remove(&t->wait_slot.node);
}

void TaskAbortWait(TaskObject *t, Err err)
{
    if (t->trap_frame)
        ArchSetInFrame(t->trap_frame, 0, err);
    t->pending_reply_cap = NULL;
    SchedUnblock(t, WAKE_IPC);
    SchedAdd(t);
}

void TaskMaybeSignalBind(TaskObject *task)
{
    if (task->bound_ev && task->bound_ev->alive && (task->state == ZOMBIE || task->state == FAULTED))
        EventSignal(task->bound_ev, (1U << task->bind_bit), false);
}

void TaskTerminate(TaskObject *task, Err exit_status)
{
    if (!task)
        return;

    SpaceObject *owner = task->owner;
    TaskState entry_state = task->state;

    task->exit_status = exit_status;
    TaskUnlinkWaits(task);

    /* (a) task was a caller mid-call: its reply cap lives in its own TCB
     * storage, about to become invalid. Tell the server holding it so a
     * later Reply fails cleanly instead of reading freed memory. */
    if (task->pending_reply_cap && task->reply_holder)
    {
        task->reply_holder->reply_cap = NULL;
        task->reply_holder = NULL;
    }

    /* (b) task was holding a reply cap (received a Call, hasn't Replied
     * yet): wake its caller with ERR_DEAD instead of leaving it blocked
     * forever. */
    if (task->reply_cap)
    {
        TaskObject *caller = task->reply_cap->caller_task;
        if (caller && caller->tid == task->reply_cap->caller_tid && caller->state != ZOMBIE &&
            caller->ipc_state == IPC_WAITING)
        {
            TaskAbortWait(caller, ERR_DEAD);
            caller->reply_holder = NULL;
        }
        task->reply_cap = NULL;
    }

    KillTask(task); // state = ZOMBIE
    TaskMaybeSignalBind(task);
    if (owner && entry_state != ZOMBIE)
        owner->live_tasks--;
    bool space_hollow = owner && owner->live_tasks == 0;

    if (space_hollow && !owner->torn_down)
    {
        owner->last_exit_status = exit_status;
        WakeWaitList(&owner->waiters, exit_status);
        SpaceMaybeSignalBind(owner);
    }

    WakeJoinTask(task, exit_status);

    if (space_hollow)
    {
        if (!owner->torn_down)
        {
            if (task == current_task)
                SchedQueueDestroyProcess(owner);
            else if (owner->parent_spid == -1)
            {
                // ResurrectRootSvc(task, exit_status);
            }
        }
    }
    else if (task == current_task)
    {
        /* Can't free our own kernel stack while running on it. */
        SchedQueueDestroyThread(task);
    }
    else
    {
        TaskDestroy(task);
    }
}

void TaskFault(TaskObject *task, Err reason)
{
    task->state = FAULTED;
    task->fault_reason = reason;
    task->owner->frozen = true;
    task->owner->faulted_tid = task->tid;
    TaskMaybeSignalBind(task);
    while (!list_empty(&task->joiners)) {
        ListNode *jn = list_pop_front(&task->joiners);
        TaskObject *joiner = container_of(jn, WaitSlot, node)->owner;
        if (joiner->trap_frame) {
            ArchSetInFrame(joiner->trap_frame, 0, ZUZU_OK);
            ArchSetInFrame(joiner->trap_frame, 1, WAKE_FAULT);
            ArchSetInFrame(joiner->trap_frame, 2, reason);
            ArchSetInFrame(joiner->trap_frame, 3, 0);
        }
        SchedUnblock(joiner, WAKE_IPC);
        SchedAdd(joiner);
    }

    ListNode *n = task->owner->tasks.node.next;
    while (n != &task->owner->tasks.node) {
        TaskObject *t = container_of(n, TaskObject, space_node);
        if (t != task && t->state == READY && t->node.next)
            SchedRemoveRunQueue(t);
        n = n->next;
    }
}
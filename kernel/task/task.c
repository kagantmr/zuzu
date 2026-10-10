#include "kernel/ipc/msg.h"
#include "kernel/mm/alloc.h"
#include "kernel/mm/pmm/pmm.h"
#include "kernel/mm/vmm/vmm.h"
#include "kernel/sched/sched.h"
#include "kernel/space/space.h"
#include "kstack.h"
#include <stddef.h>
#include <string.h>
#include <zuzu/err.h>

#define MAX_TASKS 1024

#define LOG_FMT(fmt) "(task) " fmt
#include "core/ensure.h"
#include <util/log.h>

static Tid next_tid = 1;
static TaskObject *task_table[MAX_TASKS];
static KSlabCache task_cache;

static Tid RegisterTask(TaskObject *task)
{
    if (!task)
        return 0;

    /* Advance next_tid until its hashed slot is free, so the assigned tid
     * always satisfies tid % MAX_TASKS == slot, which keeps
     * TaskObjectUnregister O(1). */
    Tid start = next_tid % MAX_TASKS;
    Tid slot = start;
    while (task_table[slot] != NULL) {
        next_tid++;
        slot = next_tid % MAX_TASKS;
        if (slot == start) {
            return 0;
        }
    }

    task->tid = next_tid++;
    task_table[slot] = task;

    KTRACE("task register: tid=%u slot=%d owner_spid=%u owner_name=%s", task->tid, slot,
           (task->owner ? task->owner->spid : 0), (task->owner ? task->owner->name : "<none>"));

    return task->tid;
}

static void TaskObjectUnregister(TaskObject *task)
{
    if (!task || task->tid == 0)
        return;

    uint32_t slot = (uint32_t)task->tid % MAX_TASKS;
    if (task_table[slot] == task)
        task_table[slot] = NULL;
}

static void SetExitResult(CpuState *frame, Err value, TaskWaitOutcome outcome)
{
    ArchSetInFrame(frame, 0, ZUZU_OK);
    ArchSetInFrame(frame, 1, (Register)value);
    ArchSetInFrame(frame, 3, outcome);
}

static void WakeWaiters(ListHead *list, Err value, TaskWaitOutcome outcome)
{
    while (!ListIsEmpty(list)) {
        ListNode *node = ListPopFront(list);
        TaskObject *waiter = container_of(node, WaitSlot, node)->owner;
        if (waiter->trap_frame)
            SetExitResult(waiter->trap_frame, value, outcome);
        SchedUnblock(waiter);
        SchedAdd(waiter);
    }
}

void WakeWaitList(ListHead *list, Err status) { WakeWaiters(list, status, TASK_EXITED); }

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
    if (t->ref_count == 0 && t->released) {
        KSlabFree(&task_cache, t);
    } else if (t->ref_count == 0 && t->state == TASK_STATE_FROZEN) {
        /* Never started and nobody can start it now: nothing else will ever
         * reap it, so its kernel stack and TCB slot would leak. */
        TaskDestroy(t); /* also frees the object: ref_count is 0 */
    }
}

void TaskDestroy(TaskObject *task)
{
    if (!task || task->released)
        return;
    TaskUnlinkWaits(task);
    TaskObjectUnregister(task);
    if (fpu_owner == task)
        fpu_owner = NULL;
    // may already be unlinked by TaskTerminate, guard is safe
    if (task->space_node.prev && task->space_node.next)
        ListRemove(&task->space_node);
    SpaceObject *owner = task->owner;
    if (owner && task->state != TASK_STATE_ZOMBIE)
        owner->live_tasks--;
    /* Release the TCB slot; scrub it so a reused slot never shows a
     * previous task's tid/spid. tcb_page_pa == 0 means the page is
     * already gone (Space teardown fail paths). */
    if (owner && task->tcb_slot < TCB_MAX_SLOTS &&
        owner->tcb_page_pa[task->tcb_slot / SLOTS_PER_PAGE]) {
        memset((void *)TcbSlotKernelVa(owner, task->tcb_slot), 0, TCB_SLOT_SIZE);
        TcbSlotFree(owner, task->tcb_slot);
    }
    if (owner && owner->main_task == task)
        owner->main_task = NULL;
    if (task->kernel_stack_top)
        KStackFree(task->kernel_stack_top);

    if (owner && owner->torn_down && ListIsEmpty(&owner->tasks))
        SpaceFinalize(owner);

    ObserverClear(&task->observers);
    task->released = true;
    task->owner = NULL;
    if (task->ref_count == 0)
        KSlabFree(&task_cache, task);
}

void TaskWaitExit(TaskObject *task, Duration timeout, CpuState *frame)
{
    ENSURE_ERR(frame, task != current_task, ERR_BADARG);
    if (task->state == TASK_STATE_ZOMBIE) {
        SetExitResult(frame, task->exit_status, TASK_EXITED);
        return;
    }
    if (task->state == TASK_STATE_FAULTED) {
        SetExitResult(frame, task->fault_reason, TASK_FAULTED);
        return;
    }
    SchedBlockOn(&task->joiners, timeout);
}

TaskObject *TaskCreate(SpaceObject *owner)
{
    if (!owner)
        return NULL;

    if (!task_cache.obj_size)
        KSlabInit(&task_cache, sizeof(TaskObject));
    TaskObject *task = KSlabAlloc(&task_cache);
    if (!task)
        return NULL;
    memset(task, 0, sizeof(*task));
    ObserverInit(&task->observers);

    task->kernel_stack_top = KStackAlloc();
    if (!task->kernel_stack_top) {
        KSlabFree(&task_cache, task);
        return NULL;
    }

    task->tid = RegisterTask(task);
    if (task->tid == 0) {
        KStackFree(task->kernel_stack_top);
        KSlabFree(&task_cache, task);
        return NULL;
    }

    task->owner = owner;
    task->sleep_slot = -1;
    ListInit(&task->joiners);
    task->state = TASK_STATE_FROZEN;
    task->ipc_state = IPC_NONE;
    task->priority = SCHED_PRIO_DEFAULT;
    task->base_prio = SCHED_PRIO_DEFAULT;
    task->time_slice = 5;
    task->max_prio = SCHED_PRIORITY_LEVELS - 1;
    task->tcb_slot = TCB_SLOT_NONE;

    int tcb_slot_idx = TcbSlotAlloc(owner);
    if (tcb_slot_idx < 0) {
        TaskObjectUnregister(task);
        KStackFree(task->kernel_stack_top);
        KSlabFree(&task_cache, task);
        return NULL;
    }
    /* Space creation only backs TCB page 0; slots on later pages get theirs
     * here, on first use (they are freed with the Space's anon regions). */
    uint32_t tcb_page = (uint32_t)tcb_slot_idx / SLOTS_PER_PAGE;
    if (!owner->tcb_page_pa[tcb_page]) {
        PhysAddr pa = PmmAllocFrame();
        if (!pa || !VmmMapUserPage(owner->as, pa, owner->tcb_page_va + (tcb_page * PAGE_SIZE),
                                   VM_PROT_USER | PROT_READ | PROT_WRITE)) {
            if (pa)
                PmmFreeFrame(pa);
            TcbSlotFree(owner, tcb_slot_idx);
            TaskObjectUnregister(task);
            KStackFree(task->kernel_stack_top);
            KSlabFree(&task_cache, task);
            return NULL;
        }
        memset((void *)PA_TO_VA(pa), 0, PAGE_SIZE);
        owner->tcb_page_pa[tcb_page] = pa;
    }
    ThreadLocalData *tcb = (ThreadLocalData *)TcbSlotKernelVa(owner, (uint32_t)tcb_slot_idx);
    VirtAddr tcb_va = TcbSlotUserVa(owner, (uint32_t)tcb_slot_idx);
    memset(tcb, 0, TCB_SLOT_SIZE);
    tcb->msg_buf = (void *)(tcb_va + offsetof(ThreadLocalData, buf));
    tcb->tid = task->tid;
    tcb->spid = owner->spid;
    task->task_info_va = tcb_va;
    task->tcb_slot = (uint8_t)tcb_slot_idx;
    task->msg_buf_phys_addr =
        TcbSlotPa(owner, (uint32_t)tcb_slot_idx) + offsetof(ThreadLocalData, buf);

    ListAddTail(&task->space_node, &owner->tasks.node);
    owner->live_tasks++;

    if (!owner->main_task)
        owner->main_task = task;

    KTRACE("task create: tid=%u owner_spid=%u owner_name=%s state=%u kernel_stack_top=%p",
           task->tid, owner->spid, owner->name, task->state, (void *)task->kernel_stack_top);

    return task;
}

void TaskUnlinkWaits(TaskObject *t)
{
    if (!t)
        return;
    if (t->node.prev && t->node.next)
        SchedRemoveRunQueue(t);
    SchedRemoveSleepQueue(t);
    if (t->wait_slot.node.prev && t->wait_slot.node.next)
        ListRemove(&t->wait_slot.node);
}

void TaskAbortWait(TaskObject *t, Err err)
{
    if (t->trap_frame)
        ArchSetInFrame(t->trap_frame, 0, err);
    t->pending_reply_cap = NULL;
    SchedUnblock(t);
    SchedAdd(t);
}

bool TaskIsDead(const TaskObject *task)
{
    return task->state == TASK_STATE_ZOMBIE || task->state == TASK_STATE_FAULTED;
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
    if (task->pending_reply_cap && task->reply_holder) {
        task->reply_holder->reply_cap = NULL;
        TaskRecomputePriority(task->reply_holder);
        task->reply_holder = NULL;
    }

    /* (b) task was holding a reply cap (received a Call, hasn't Replied
     * yet): wake its caller with ERR_DEAD instead of leaving it blocked
     * forever. */
    if (task->reply_cap) {
        TaskObject *caller = task->reply_cap->caller_task;
        if (caller && caller->tid == task->reply_cap->caller_tid &&
            caller->state != TASK_STATE_ZOMBIE && caller->ipc_state == IPC_WAITING) {
            TaskAbortWait(caller, ERR_DEAD);
            caller->reply_holder = NULL;
        }
        task->reply_cap = NULL;
    }

    task->state = TASK_STATE_ZOMBIE;
    ObserverNotify(&task->observers);
    if (owner && entry_state != TASK_STATE_ZOMBIE)
        owner->live_tasks--;
    bool space_hollow = owner && owner->live_tasks == 0;

    if (space_hollow && !owner->torn_down) {
        owner->last_exit_status = exit_status;
        WakeWaitList(&owner->waiters, exit_status);
        ObserverNotify(&owner->observers);
    }

    WakeWaitList(&task->joiners, exit_status);

    /* The last task of a Space stays a zombie until the Space is destroyed. */
    if (space_hollow)
        return;
    if (task == current_task) {
        /* Can't free our own kernel stack while running on it. */
        SchedQueueDestroyTask(task);
    } else {
        TaskDestroy(task);
    }
}

void TaskFault(TaskObject *task, Err reason)
{
    task->state = TASK_STATE_FAULTED;
    task->fault_reason = reason;
    task->owner->frozen = true;
    ObserverNotify(&task->observers);
    WakeWaiters(&task->joiners, reason, TASK_FAULTED);

    ListNode *n = task->owner->tasks.node.next;
    while (n != &task->owner->tasks.node) {
        TaskObject *t = container_of(n, TaskObject, space_node);
        if (t != task && t->state == TASK_STATE_READY && t->node.next)
            SchedRemoveRunQueue(t);
        n = n->next;
    }
}

#define PRIORITY_PROPAGATION_DEPTH 8

static void RecomputePriorityAt(TaskObject *t, unsigned depth)
{
    Prio eff = t->base_prio;
    ReplyObject *rc = t->reply_cap;
    if (rc) {
        TaskObject *caller = rc->caller_task;
        if (caller && caller->tid == rc->caller_tid && caller->state != TASK_STATE_ZOMBIE &&
            caller->ipc_state == IPC_WAITING && caller->priority > eff)
            eff = caller->priority;
    }

    if (eff == t->priority)
        return;

    SchedSetEffective(t, eff);

    if (t->reply_holder && depth < PRIORITY_PROPAGATION_DEPTH)
        RecomputePriorityAt(t->reply_holder, depth + 1);
}

void TaskRecomputePriority(TaskObject *t) { RecomputePriorityAt(t, 0); }

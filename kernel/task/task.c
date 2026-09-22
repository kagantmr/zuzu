#include "task.h"
#include "kernel/mm/alloc.h"
#include "kernel/sched/sched.h"
#include "kstack.h"
#include <spinlock.h>
#include <string.h>

#define MAX_THREADS 1024

#define LOG_FMT(fmt) "(task) " fmt
#include <zuzu/log.h>

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
	while (task_table[slot] != NULL) {
		next_tid++;
		slot = next_tid % MAX_THREADS;
		if (slot == start) {
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

void WakeJoinTask(TaskObject *task, int32_t exit_status)
{
	if (!task)
		return;

	while (!list_empty(&task->joiners)) {
		ListNode *node = list_pop_front(&task->joiners);
		if (!node)
			break;
		TaskObject *joiner = container_of(node, TaskObject, join_node);
		joiner->wake_reason = WAKE_IPC;
		joiner->state = READY;
		if (joiner->trap_frame)
			(*arch_reg(joiner->trap_frame, 0)) = (uint32_t)exit_status;
		SchedAdd(joiner);
	}
}

void DestroyTask(TaskObject *task)
{
	if (!task)
		return;
	ThreadUnlinkWaits(task);
	TaskObjectUnregister(task);
	if (fpu_owner == task)
		fpu_owner = NULL;
	// may already be removed by tquit, guard is safe
	if (task->process_node.prev && task->process_node.next)
		list_remove(&task->process_node);
	SpaceObject *owner = task->owner;
	/* Release the TCB slot; scrub it so a reused slot never shows a
	 * previous task's tid/pid. tcb_page_pa == 0 means the page is
	 * already gone (process teardown fail paths). */
	if (owner && task->tcb_slot < TCB_MAX_SLOTS &&
	    owner->tcb_page_pa[task->tcb_slot / SLOTS_PER_PAGE]) {
		memset((void *)TcbSlotKVirtAddr(owner, task->tcb_slot), 0, TCB_SLOT_SIZE);
		TcbSlotFree(owner, task->tcb_slot);
	}
	if (owner && owner->task == task)
		owner->task = NULL;
	if (task->kernel_stack_top)
		KernelStackFree(task->kernel_stack_top);
	KSlabFree(&task_cache, task);
}

TaskObject *CreateTask(SpaceObject *owner)
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
	if (!task->kernel_stack_top) {
		KSlabFree(&task_cache, task);
		return NULL;
	}

	task->tid = RegisterTask(task);
	if (task->tid == 0) {
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
	task->process_node.next = NULL;
	task->process_node.prev = NULL;
	task->timeout_node.next = NULL;
	task->timeout_node.prev = NULL;
	list_init(&task->joiners);
	task->join_node.next = NULL;
	task->join_node.prev = NULL;
	task->wake_reason = WAKE_NONE;
	task->wake_deadline = 0;
	task->state = FROZEN;
	task->ipc_state = IPC_NONE;
	task->blocked_port = NULL;
	task->pending_reply_cap = NULL;
	task->lmsg_buf_phys_addr = 0;
	task->lmsg_buf_xfer_len = 0;
	task->priority = SCHED_PRIO_DEFAULT;
	task->time_slice = 5;
	task->ticks_remaining = task->time_slice;
	task->slice_deadline = 0;
	task->task_info_va = 0;
	task->tcb_slot = TCB_SLOT_NONE;

	list_add_tail(&task->process_node, &owner->tasks.node);

	if (!owner->task)
		owner->task = task;

	KTRACE("task create: tid=%u owner_pid=%u owner_name=%s state=%u kernel_stack_top=%p",
	       task->tid, owner->pid, owner->name, task->state,
	       (void *)task->kernel_stack_top);

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

void ThreadUnlinkWaits(TaskObject *t)
{
    if (!t) return;
    if (t->node.prev && t->node.next)                     list_remove(&t->node);
    if (t->join_node.prev && t->join_node.next)           list_remove(&t->join_node);
    SchedRemoveSleepQueue(t);
    if (t->ntfn_wait_slot.node.prev && t->ntfn_wait_slot.node.next)
        list_remove(&t->ntfn_wait_slot.node);
    if (t->port_wait_slot.node.prev && t->port_wait_slot.node.next)
        list_remove(&t->port_wait_slot.node);
}


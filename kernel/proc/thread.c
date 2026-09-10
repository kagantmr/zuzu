#include "thread.h"
#include "kernel/mm/alloc.h"
#include "kernel/sched/sched.h"
#include "kstack.h"
#include "process.h"
#include <spinlock.h>
#include <string.h>

#define MAX_THREADS 1024

#define LOG_FMT(fmt) "(thread) " fmt
#include <zuzu/log.h>

static Tid next_tid = 1;
static Thread *thread_table[MAX_THREADS];
static KHeapSlabCache thread_cache;

static Tid ThreadRegister(Thread *thread)
{
	if (!thread)
		return 0;


	/* Advance next_tid until its hashed slot is free, so the assigned tid
	 * always satisfies tid % MAX_THREADS == slot. ThreadFindByTid and
	 * thread_unregister rely on that to stay O(1). Mirrors process_table. */
	Tid start = next_tid % MAX_THREADS;
	Tid slot = start;
	while (thread_table[slot] != NULL) {
		next_tid++;
		slot = next_tid % MAX_THREADS;
		if (slot == start) {
			return 0;
		}
	}

	thread->tid = next_tid++;
	thread_table[slot] = thread;

	KTRACE("thread register: tid=%u slot=%d owner_pid=%u owner_name=%s", thread->tid, slot,
	       (thread->owner_process ? thread->owner_process->pid : 0),
	       (thread->owner_process ? thread->owner_process->name : "<none>"));

	return thread->tid;
}

static void ThreadUnregister(Thread *thread)
{
	if (!thread || thread->tid == 0)
		return;


	uint32_t slot = (uint32_t)thread->tid % MAX_THREADS;
	if (thread_table[slot] == thread)
		thread_table[slot] = NULL;

}

void ThreadKill(Thread *thread)
{
	if (!thread)
		return;

	thread->state = ZOMBIE;
}

void ThreadDestroy(Thread *thread)
{
	if (!thread)
		return;
	ThreadUnlinkWaits(thread);
	ThreadUnregister(thread);
	if (fpu_owner == thread)
		fpu_owner = NULL;
	// may already be removed by tquit, guard is safe
	if (thread->process_node.prev && thread->process_node.next)
		list_remove(&thread->process_node);
	ProcessObj *owner = thread->owner_process;
	/* Release the TCB slot; scrub it so a reused slot never shows a
	 * previous thread's tid/pid. tcb_page_pa == 0 means the page is
	 * already gone (process teardown fail paths). */
	if (owner && thread->tcb_slot < TCB_MAX_SLOTS &&
	    owner->tcb_page_pa[thread->tcb_slot / SLOTS_PER_PAGE]) {
		memset((void *)TcbSlotKVirtAddr(owner, thread->tcb_slot), 0, TCB_SLOT_SIZE);
		TcbSlotFree(owner, thread->tcb_slot);
	}
	if (owner && owner->thread == thread)
		owner->thread = NULL;
	if (thread->kernel_stack_top)
		KernelStackFree(thread->kernel_stack_top);
	KSlabFree(&thread_cache, thread);
}

Thread *ThreadCreate(ProcessObj *owner_process)
{
	if (!owner_process)
		return NULL;

	if (!thread_cache.obj_size)
		KSlabInit(&thread_cache, "Thread", sizeof(Thread));
	Thread *thread = KSlabAlloc(&thread_cache);
	if (!thread)
		return NULL;
	memset(thread, 0, sizeof(*thread));

	thread->kernel_stack_top = KernelStackAlloc();
	if (!thread->kernel_stack_top) {
		KSlabFree(&thread_cache, thread);
		return NULL;
	}

	thread->tid = ThreadRegister(thread);
	if (thread->tid == 0) {
		KernelStackFree(thread->kernel_stack_top);
		KSlabFree(&thread_cache, thread);
		return NULL;
	}

	thread->kernel_sp = NULL;
	thread->trap_frame = NULL;
	thread->owner_process = owner_process;
	thread->exit_status = 0;
	thread->node.next = NULL;
	thread->node.prev = NULL;
	thread->sleep_slot = -1;
	thread->process_node.next = NULL;
	thread->process_node.prev = NULL;
	thread->timeout_node.next = NULL;
	thread->timeout_node.prev = NULL;
	thread->wake_reason = WAKE_NONE;
	thread->wake_deadline = 0;
	thread->state = FROZEN;
	thread->ipc_state = IPC_NONE;
	thread->blocked_port = NULL;
	thread->pending_reply_cap = NULL;
	thread->lmsg_buf_phys_addr = 0;
	thread->lmsg_buf_xfer_len = 0;
	thread->priority = SCHED_PRIO_DEFAULT;
	thread->time_slice = 5;
	thread->ticks_remaining = thread->time_slice;
	thread->slice_deadline = 0;
	thread->thread_info_va = 0;
	thread->tcb_slot = TCB_SLOT_NONE;

	list_add_tail(&thread->process_node, &owner_process->threads.node);

	if (!owner_process->thread)
		owner_process->thread = thread;

	KTRACE("thread create: tid=%u owner_pid=%u owner_name=%s state=%u kernel_stack_top=%p",
	       thread->tid, owner_process->pid, owner_process->name, thread->state,
	       (void *)thread->kernel_stack_top);

	return thread;
}

Thread *ThreadFindByTid(Tid tid)
{
	if (tid == 0)
		return NULL;

	uint32_t slot = (uint32_t)tid % MAX_THREADS;
	Thread *t = thread_table[slot];
	if (t && t->tid == tid)
		return t;
	return NULL;
}

void ThreadUnlinkWaits(Thread *t)
{
    if (!t) return;
    if (t->node.prev && t->node.next)                     list_remove(&t->node);
    SchedRemoveSleepQueue(t);
    if (t->ntfn_wait_slot.node.prev && t->ntfn_wait_slot.node.next)
        list_remove(&t->ntfn_wait_slot.node);
    if (t->port_wait_slot.node.prev && t->port_wait_slot.node.next)
        list_remove(&t->port_wait_slot.node);
    ThreadWaitanyClearWaits(t);
    ThreadWaitanyClearPortWaits(t);
}


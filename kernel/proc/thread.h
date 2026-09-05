#ifndef ZUZU_THREAD_H
#define ZUZU_THREAD_H

#include "kernel/ipc/ntfn.h"
#include "kernel/ipc/port.h"
#include "kernel/mm/vmm.h"
#include <arch/fpu.h>
#include <arch/regs.h>
#include <list.h>
#include <zuzu/types.h>

typedef struct process Process;

typedef enum thread_state {
	READY = 0, // ready to run, in run queue
	RUNNING,   // on CPU
	BLOCKED,   // waiting for IPC or timeout
	ZOMBIE,	   // called quit()
	FROZEN,	   // not runnable yet
} ThreadState;

typedef enum {
	WAKE_NONE = 0, // not currently sleeping/waiting
	WAKE_IPC,      // woken by IPC partner
	WAKE_TIMEOUT,  // woken by timer
} WakeReason;

typedef enum ipc_state {
	IPC_NONE = 0,
	IPC_SENDER,
	IPC_RECEIVER,
	IPC_WAITING,
} MsgState;

#ifndef WAITANY_MAX_HANDLES
#define WAITANY_MAX_HANDLES 16u
#endif

typedef struct thread Thread;

#define TCB_SLOT_NONE 0xFFu /* thread holds no TCB slot */

typedef struct thread_wait_slot {
	ListNode node;
	Thread *owner;
	uint32_t index;
} ThreadWaitSlot;

struct thread {
	VirtAddr kernel_stack_top; // base of kernel stack for freeing (offset 0)
	CpuState
	    *trap_frame; // pointer to saved user registers for IPC and context switching (offset 4)
	Tid tid;	 // thread ID (offset 8)
	uint32_t *kernel_sp; // current kernel stack pointer for context switching (offset 12 -
			     // CRITICAL: switch.S offset)
	int32_t exit_status;
	ListNode node;	       // embedded, not pointers
	ListNode process_node; // membership in owner process thread list
	ListNode timeout_node;
	WakeReason wake_reason;
	Tick wake_tick;
	ThreadState state;
	ListNode destroy_node;
	MsgState ipc_state;
	Port *blocked_port;
	ReplyCap *pending_reply_cap;
	PhysAddr lmsg_buf_phys_addr;
	size_t lmsg_buf_xfer_len;
	Marker port_marker;
	ThreadWaitSlot ntfn_wait_slot;
	ThreadWaitSlot waitany_wait_slots[WAITANY_MAX_HANDLES];
	NtfnObj *waitany_wait_ntfns[WAITANY_MAX_HANDLES];
	size_t waitany_wait_count;
	uint32_t waitany_wait_match_index;
	uint32_t waitany_wait_bits;
	bool waitany_active;
	ThreadWaitSlot port_wait_slot;				     /* for msg_recv */
	ThreadWaitSlot waitany_port_wait_slots[WAITANY_MAX_HANDLES]; /* for waitany endpoints */
	Port *waitany_wait_ports[WAITANY_MAX_HANDLES];
	size_t waitany_port_wait_count;
	bool waitany_port_wait_active;
	uint32_t waitany_port_wait_match_index;
	WaitanyResult waitany_pending_result;
	uint32_t priority, time_slice, ticks_remaining;
	Process *owner_process; // backpointer to owning process
	VirtAddr thread_info_va;
	uint8_t tcb_slot;   // index into owner's TCB page, TCB_SLOT_NONE if unassigned
	FpuState fpu_state; // lazily saved/restored, see kernel/sched/sched.c fpu_owner
#ifdef ZUZU_BENCH
	uint32_t bench_irq_wait_start; // PMCCNTR at SysNtfnWait block, for the IRQ-wait bench
#endif
};

#ifdef __cplusplus
static_assert(offsetof(thread_t, kernel_sp) == 12,
	      "switch.S expects process->kernel_sp at offset 12");
#else
_Static_assert(offsetof(Thread, kernel_sp) == 12,
	       "switch.S expects process->kernel_sp at offset 12");
#endif

void ThreadDestroy(Thread *thread);
Thread *ThreadCreate(Process *owner_process);
void ThreadKill(Thread *thread);
Thread *ThreadFindByTid(Tid tid);

static inline void ThreadWaitanyClearWaits(Thread *thread)
{
	if (!thread || !thread->waitany_active)
		return;

	for (uint32_t i = 0; i < thread->waitany_wait_count && i < WAITANY_MAX_HANDLES; i++) {
		ListNode *node = &thread->waitany_wait_slots[i].node;
		if (node->prev && node->next)
			list_remove(node);
		thread->waitany_wait_ntfns[i] = NULL;
		node->prev = NULL;
		node->next = NULL;
	}

	thread->waitany_wait_count = 0;
	thread->waitany_wait_match_index = WAITANY_NO_MATCH;
	thread->waitany_wait_bits = 0;
	thread->waitany_active = false;
}

static inline void ThreadWaitanyClearPortWaits(Thread *thread)
{
	if (!thread || !thread->waitany_port_wait_active)
		return;

	for (uint32_t i = 0; i < thread->waitany_port_wait_count && i < WAITANY_MAX_HANDLES; i++) {
		ListNode *node = &thread->waitany_port_wait_slots[i].node;
		if (node->prev && node->next)
			list_remove(node);
		thread->waitany_wait_ports[i] = NULL;
		node->prev = NULL;
		node->next = NULL;
	}

	thread->waitany_port_wait_count = 0;
	thread->waitany_port_wait_match_index = WAITANY_NO_MATCH;
	thread->waitany_port_wait_active = false;
}

void ThreadUnlinkWaits(Thread *t);

#endif // ZUZU_THREAD_H

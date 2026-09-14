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

typedef enum {
	WAIT_KIND_NONE = 0,
	WAIT_KIND_NTFN,
	WAIT_KIND_PORT,
} WaitKind;

/**
 * @brief A registration linked into one ntfn's wait_queue or one port's
 * receiver_queue. Used both for plain single-handle waits (ntfn_wait_slot,
 * port_wait_slot below) and for every slot of a waitany call; see
 * kernel/ipc/waitslot.h.
 */
typedef struct wait_slot {
	ListNode node;
	Thread *owner;
	WaitKind kind;
	uint32_t handle_index; /**< waitany: index into caller's handles[];
				     unused for a plain single-handle wait. */
	union {
		NtfnObj *ntfn;
		Port *port;
	};
} WaitSlot;

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
	ListHead joiners;
	ListNode join_node;
	WakeReason wake_reason;
	uint64_t wake_deadline;
	int16_t sleep_slot;
	ThreadState state;
	ListNode destroy_node;
	MsgState ipc_state;
	Port *blocked_port;
	ReplyCap *pending_reply_cap;
	PhysAddr lmsg_buf_phys_addr;
	size_t lmsg_buf_xfer_len;
	Marker port_marker;
	WaitSlot ntfn_wait_slot;  /* for SysNtfnWait */
	WaitSlot port_wait_slot;  /* for SysMsgRecv */
	WaitSlot waitany_slots[WAITANY_MAX_HANDLES];
	uint32_t waitany_slot_count;
	bool waitany_registered;
	uint32_t waitany_match_index;
	WaitanyResult waitany_pending_result;
	/* Set immediately before SysWaitAny blocks, cleared the instant
	 * Schedule() hands the thread back. A thread that reaches userspace
	 * with this still set never finished its syscall -- see the check at
	 * SyscallDispatch entry. */
	bool waitany_in_block;
	uint32_t priority, time_slice, ticks_remaining;
	/* Absolute counter value (ArchTimerNow() units) at which this thread's
	 * slice expires. Set on dispatch; compared against, never decremented,
	 * so slice expiry no longer depends on a periodic tick arriving. */
	uint64_t slice_deadline;
	Process *owner_process; // backpointer to owning process
	VirtAddr thread_info_va;
	uint8_t tcb_slot;   // index into owner's TCB page, TCB_SLOT_NONE if unassigned
	FpuState fpu_state; // lazily saved/restored, see kernel/sched/sched.c fpu_owner
#ifdef CONFIG_ZUZU_BENCH
	uint32_t bench_irq_wait_start; // PMCCNTR at SysNtfnWait block, for the IRQ-wait bench
#endif
};

_Static_assert(offsetof(Thread, kernel_sp) == 12,
	       "switch.S expects process->kernel_sp at offset 12");

void ThreadDestroy(Thread *thread);
Thread *ThreadCreate(Process *owner_process);
void ThreadKill(Thread *thread);
void ThreadWakeJoiners(Thread *thread, int32_t exit_status);
Thread *ThreadFindByTid(Tid tid);

void ThreadUnlinkWaits(Thread *t);

#endif // ZUZU_THREAD_H

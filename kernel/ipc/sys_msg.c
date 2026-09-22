#include "sys_msg.h"
#include "core/panic.h"
#include "handle.h"
#include "kernel/layout.h"
#include "kernel/mm/alloc.h"
#include "kernel/proc/kstack.h"
#include "kernel/sched/sched.h"
#include "kernel/svc/svc.h"
#include "port.h"
#include <arch/timer.h>
#include <compiler.h>
#include <stdbool.h>
#include <string.h>
#include <zuzu/tls.h>
#include <zuzu/types.h>

#include "kernel/irq/sys_irq.h"
#define LOG_FMT(fmt) "(ipc) " fmt
#include <zuzu/log.h>

extern kernel_layout_t kernel_layout;

#ifdef CONFIG_ZUZU_BENCH

#include "kernel/bench.h"

BENCH_STAT(g_bench_ipc_buf_copy_memcpy, "ipc_buf_copy: memcpy");
BENCH_STAT(g_bench_ipc_buf_copy_wordcopy, "ipc_buf_copy: hand-rolled word-copy");
static uint8_t g_bench_wordcopy_scratch[LMSG_BUF_SIZE] __attribute__((aligned(4)));
#endif

/* Every call site passes a sender and a receiver -- never the same thread
 * (and their ipc_buf_pa pages are always separate physical frames), so
 * the memcpy below is genuinely non-overlapping. */
static void LmsgBufCopy(TaskObject *restrict src, TaskObject *restrict dst, uint32_t len)
{
	if (!len || !src->lmsg_buf_phys_addr || !dst->lmsg_buf_phys_addr)
		return;
	if (len > LMSG_BUF_SIZE)
		return;

	const void *srcp = (const void *)PA_TO_VA(src->lmsg_buf_phys_addr);
	void *dstp = (void *)PA_TO_VA(dst->lmsg_buf_phys_addr);

#ifdef CONFIG_ZUZU_BENCH
	uint32_t bench_start = BENCH_BEGIN();
#endif
	memcpy(dstp, srcp, len);
#ifdef CONFIG_ZUZU_BENCH
	BENCH_END(g_bench_ipc_buf_copy_memcpy, bench_start);

	/* Swap-test: hand-rolled word-copy loop timed against the same source
	 * buffer, on a scratch destination so it can't corrupt the real reply.
	 * Only fires for word-aligned, word-multiple lengths -- the case real
	 * IPC payloads mostly are -- since the loop below has no unaligned-tail
	 * handling. If this alone recovers a big chunk of the RTT's mystery
	 * cycles, the answer was memcpy() overhead, not the walk. */
	if (((uintptr_t)srcp & 3u) == 0 && (len & 3u) == 0) {
		bench_start = BENCH_BEGIN();
		const uint32_t *ws = (const uint32_t *)srcp;
		uint32_t *wd = (uint32_t *)(void *)g_bench_wordcopy_scratch;
		uint32_t nwords = len / 4u;
		for (uint32_t i = 0; i < nwords; i++)
			wd[i] = ws[i];
		BENCH_END(g_bench_ipc_buf_copy_wordcopy, bench_start);
	}
#endif
}

#ifdef DEBUG
static bool IsFrameNormal(const CpuState *tf)
{
	uintptr_t p = (uintptr_t)tf;
	if (p == 0 || (p & 0x3U) != 0)
		return false;

	bool in_stack = false;
	if (kernel_layout.stack_base_va && kernel_layout.stack_top_va &&
	    p >= kernel_layout.stack_base_va && p + sizeof(CpuState) <= kernel_layout.stack_top_va)
		in_stack = true;

	if (p >= KSTACK_REGION_BASE && p + sizeof(CpuState) <= KSTACK_REGION_TOP)
		in_stack = true;

	if (!in_stack)
		return false;

	/* Content check: every frame handled by the IPC paths belongs to a user
	 * thread blocked in a syscall, so its saved return PC must be a nonzero
	 * user VA. A zero/kernel PC here means the frame was clobbered (e.g. by
	 * a nested exception) and would resume user mode into a fault. */
	Register pc = arch_regs_pc(tf);
	if (pc == 0 || pc >= USER_VA_TOP)
		return false;

	return true;
}


static __cold __noinline void PanicBadFrame(const char *where, const SpaceObject *owner,
						       const CpuState *tf)
{
	if (tf && ((uintptr_t)tf & 0x3U) == 0)
		KERROR("  frame: pc=%p lr=%p sp=%p cpsr=%p", (void *)arch_regs_pc(tf),
		       (void *)arch_regs_lr(tf), (void *)arch_regs_sp(tf),
		       (void *)arch_regs_flags(tf));
	panic("Corrupt trap_frame in IPC path at %s: owner_pid=%u tf=%p current_pid=%u", where,
	      (unsigned)(owner ? owner->pid : 0), (const void *)tf,
	      (unsigned)(current_task && current_task->owner_process
			     ? current_task->owner_process->pid
			     : 0));
}
#endif

static __always_inline void CancelTimeout(TaskObject *t)
{
	if (t->wake_deadline != 0) {
		SchedRemoveSleepQueue(t);
		t->wake_deadline = 0;
	}
}

static __hot inline void IpcWakeThread(TaskObject *t)
{
	t->ipc_state = IPC_NONE;
	t->blocked_port = NULL;
	CancelTimeout(t);
	t->wake_reason = WAKE_IPC;
	t->state = READY;
	SchedAdd(t);
}

#ifdef CONFIG_ZUZU_BENCH
BENCH_STAT(g_bench_handle_lookup, "handle table lookup");
BENCH_STAT(g_bench_direct_handoff, "IPC direct-switch handoff");
BENCH_STAT(g_bench_call_body, "SysMsgCall body (pre-switch)");
BENCH_STAT(g_bench_reply_body, "SysMsgReply body (whole)");
BENCH_STAT(g_bench_recv_body, "SysMsgRecv body (whole)");
BENCH_STAT(g_bench_slot_findfree, "  HandleTableFindFree");
BENCH_STAT(g_bench_entry_claim, "  HandleEntryClaim");
BENCH_STAT(g_bench_entry_free, "  HandleEntryFree");
BENCH_STAT(g_bench_track_cap, "  ProcessTrackReplyCap");
BENCH_STAT(g_bench_untrack_cap, "  ProcessUntrackReplyCap");
BENCH_STAT(g_bench_validate_replycap, "  ValidateReplyCap");

/* Which of the three exits a call takes, counted rather than timed: the
 * timed handoff stat says what a handoff costs, not how often we get one.
 * A round trip that misses the handoff pays a full block+schedule. */
typedef struct {
	const char *name;
	uint32_t handoff, takers, no_receiver;
	bool reported;
} CallPathMix;

static CallPathMix g_mix_call = { .name = "msg_call path mix" };
static CallPathMix g_mix_lcall = { .name = "msg_lcall path mix" };

#define CALLPATH_HANDOFF     0
#define CALLPATH_TAKERS      1
#define CALLPATH_NO_RECEIVER 2

static void CallPathTally(CallPathMix *m, int which)
{
	if (m->reported)
		return;
	if (which == CALLPATH_HANDOFF)
		m->handoff++;
	else if (which == CALLPATH_TAKERS)
		m->takers++;
	else
		m->no_receiver++;

	uint32_t total = m->handoff + m->takers + m->no_receiver;
	if (total >= 100000u) {
		m->reported = true;
		kprintf("[BENCH] %-32s handoff=%u takers=%u no-receiver=%u (n=%u)\n", m->name,
			m->handoff, m->takers, m->no_receiver, total);
	}
}
#define CALL_TALLY(m, w) CallPathTally(&(m), (w))
#else
#define CALL_TALLY(m, w) ((void)0)
#endif

/* First thing every send/recv/call/reply does. The four checks below are
 * the "this handle turned out to be garbage" cases: in steady-state IPC
 * traffic a client hammers a port it already validated once, so all four
 * are marked unlikely to keep the fall-through (the success return) as
 * the straight-line path. */
static HandleTableEntry *__hot ValidatePortHandle(SpaceObject *proc, Handle handle, CpuState *frame)
{
	if (unlikely(!proc)) {
		arch_reg_set(frame, 0, ERR_BADARG);
		return NULL;
	}
#ifdef CONFIG_ZUZU_BENCH
	uint32_t bench_start = BENCH_BEGIN();
#endif
	HandleTableEntry *entry = HandleTableGet(&proc->handle_table, (uint32_t)handle);
#ifdef CONFIG_ZUZU_BENCH
	BENCH_END(g_bench_handle_lookup, bench_start);
#endif
	if (unlikely(!entry)) {
		arch_reg_set(frame, 0, ERR_BADHANDLE);
		return NULL;
	}
	if (unlikely(entry->type != HANDLE_PORT)) {
		arch_reg_set(frame, 0, ERR_BADTYPE);
		return NULL;
	}
	if (unlikely(!entry->port)) {
		arch_reg_set(frame, 0, ERR_BADHANDLE);
		return NULL;
	}
	if (unlikely(!entry->port->alive)) {
		arch_reg_set(frame, 0, ERR_DEAD);
		return NULL;
	}

	return entry;
}

static HandleTableEntry *ValidateReplyCap(SpaceObject *proc, Handle handle_idx, TaskObject **target_out,
					  CpuState *frame)
{
	if (!proc || handle_idx == 0) {
		arch_reg_set(frame, 0, ERR_BADHANDLE);
		return NULL;
	}

	HandleTableEntry *entry = HandleTableGet(&proc->handle_table, (uint32_t)handle_idx);
	if (!entry) {
		arch_reg_set(frame, 0, ERR_BADHANDLE);
		return NULL;
	}
	if (entry->type != HANDLE_REPLY) {
		arch_reg_set(frame, 0, ERR_BADTYPE);
		return NULL;
	}
	if (!entry->reply || entry->reply->caller_tid == 0) {
		arch_reg_set(frame, 0, ERR_BADHANDLE);
		return NULL;
	}

	TaskObject *target = ThreadFindByTid(entry->reply->caller_tid);

	if (!target || target->state == ZOMBIE) {
		ProcessUntrackReplyCap(entry->reply);
		KFreeReplyCap(entry->reply);
		HandleEntryFree(&proc->handle_table, entry);
		arch_reg_set(frame, 0, ERR_DEAD);
		return NULL;
	}

	if (target->ipc_state != IPC_WAITING) {
		ProcessUntrackReplyCap(entry->reply);
		KFreeReplyCap(entry->reply);
		HandleEntryFree(&proc->handle_table, entry);
		arch_reg_set(frame, 0, ERR_DEAD);
		return NULL;
	}

	*target_out = target;
	return entry;
}

void __attribute__((hot)) SysMsgSend(CpuState *frame)
{
	int handle = (int)(*ArchGetFromFrame(frame, 0));

	HandleTableEntry *entry = ValidatePortHandle(current_task->owner_process, handle, frame);
	if (unlikely(!entry))
		return;
	PortObject *port = entry->port;
	if (unlikely(!port)) {
		return;
	}

	if (likely(!list_empty(&port->receiver_queue))) {
		ListNode *receiver = list_pop_front(&port->receiver_queue);
		WaitSlot *rx_slot = container_of(receiver, WaitSlot, node);
		TaskObject *rx_thread = rx_slot->owner;
		CpuState *rx_frame = rx_thread->trap_frame;
#ifdef DEBUG
		if (!IsFrameNormal(rx_frame))
			PanicBadFrame("ZuzuMsgSend.rx", rx_thread->owner_process,
						 rx_frame);
#endif
		arch_reg_set(rx_frame, 0, current_task->owner_process->pid);
		(*ArchGetFromFrame(rx_frame, 1)) = (*ArchGetFromFrame(frame, 1));
		(*ArchGetFromFrame(rx_frame, 2)) = (*ArchGetFromFrame(frame, 2));
		(*ArchGetFromFrame(rx_frame, 3)) = (*ArchGetFromFrame(frame, 3));
		rx_thread->ipc_state = IPC_NONE;
		rx_thread->blocked_port = NULL;
		CancelTimeout(rx_thread);
		rx_thread->wake_reason = WAKE_IPC;
		rx_thread->state = READY;
		SchedAdd(rx_thread);
		(*ArchGetFromFrame(frame, 0)) = 0;
	} else {
		current_task->ipc_state = IPC_SENDER;
		current_task->blocked_port = port;
		current_task->port_marker = entry->marker;
		list_add_tail(&current_task->node, &port->sender_queue.node);
		current_task->state = BLOCKED;
		Schedule();
	}
}

void __attribute__((hot)) SysMsgRecv(CpuState *frame)
{
#ifdef CONFIG_ZUZU_BENCH
	uint32_t bench_recv_start = BENCH_BEGIN();
#endif
	int handle = (int)(*ArchGetFromFrame(frame, 0));
	uint32_t timeout_ms = (*ArchGetFromFrame(frame, 1)); // TIMEOUT_POLL / TIMEOUT_INFINITE / finite ms

	HandleTableEntry *entry = ValidatePortHandle(current_task->owner_process, handle, frame);
	if (unlikely(!entry))
		return;
	PortObject *port = entry->port;
	if (unlikely(!port)) {
		return;
	}

	/* ZuzuMsgCall's direct-switch handoff (see SysMsgCall) never routes
	 * through here -- it hands the receiver its registers straight from
	 * the sender's side and wakes it directly, so in a Recv/Call-driven
	 * workload (the common one; ZuzuMsgSend+Recv is the other, slower
	 * pairing) sender_queue is essentially always empty when this runs. */
	if (unlikely(!list_empty(&port->sender_queue))) {
		ListNode *sender = list_pop_front(&port->sender_queue);
		TaskObject *sr_thread = container_of(sender, TaskObject, node);
		CpuState *sr_frame = sr_thread->trap_frame;
#ifdef DEBUG
		if (unlikely(!IsFrameNormal(sr_frame))) {
			PanicBadFrame("ZuzuMsgRecv.sr", sr_thread->owner_process,
						 sr_frame);
		}
#endif
		// Copy message to receiver
		arch_reg_set(frame, 0, sr_thread->owner_process->pid);
		(*ArchGetFromFrame(frame, 1)) = (*ArchGetFromFrame(sr_frame, 1));
		(*ArchGetFromFrame(frame, 2)) = (*ArchGetFromFrame(sr_frame, 2));
		(*ArchGetFromFrame(frame, 3)) = (*ArchGetFromFrame(sr_frame, 3));

		if (sr_thread->ipc_state == IPC_SENDER) {
			// wake the sender, it's done
			(*ArchGetFromFrame(sr_frame, 0)) = 0;
			sr_thread->ipc_state = IPC_NONE;
			sr_thread->blocked_port = NULL;
			// Cancel timeout if sender had one
			CancelTimeout(sr_thread);
			sr_thread->wake_reason = WAKE_IPC;
			sr_thread->state = READY;
			if (sr_thread->lmsg_buf_xfer_len > 0) {
				LmsgBufCopy(sr_thread, current_task,
					     sr_thread->lmsg_buf_xfer_len);
				(*ArchGetFromFrame(frame, 1)) = sr_thread->lmsg_buf_xfer_len;
				(*ArchGetFromFrame(frame, 2)) = 0;
				(*ArchGetFromFrame(frame, 3)) = 0;
				sr_thread->lmsg_buf_xfer_len = 0;
			}
			SchedAdd(sr_thread);
		} else if (sr_thread->ipc_state == IPC_WAITING) {
			// Use the pre-allocated reply cap
			ReplyCap *rc = sr_thread->pending_reply_cap;
			sr_thread->pending_reply_cap = NULL;
			// rc is guaranteed non-NULL — caller pre-allocated it

			int slot =
			    HandleTableFindFree(&current_task->owner_process->handle_table);
			if (slot < 0) {
				// Handle table full - but at least we can report the error
				// and the caller's rc gets cleaned up
				KFreeReplyCap(rc);
				sr_thread->pending_reply_cap = NULL;
				// Wake the caller with an error instead of leaving it stuck
				arch_reg_set(sr_thread->trap_frame, 0, ERR_NOMEM);
				sr_thread->ipc_state = IPC_NONE;
				sr_thread->blocked_port = NULL;
				// Cancel timeout if sender had one
				CancelTimeout(sr_thread);
				sr_thread->wake_reason = WAKE_IPC;
				sr_thread->state = READY;
				SchedAdd(sr_thread);
				arch_reg_set(frame, 0, ERR_NOMEM);
				return;
			}

			HandleTableEntry *rentry =
			    HandleTableGet(&current_task->owner_process->handle_table, (uint32_t)slot);
			if (!rentry) {
				KFreeReplyCap(rc);
				arch_reg_set(sr_thread->trap_frame, 0, ERR_NOMEM);
				sr_thread->ipc_state = IPC_NONE;
				sr_thread->blocked_port = NULL;
				CancelTimeout(sr_thread);
				sr_thread->wake_reason = WAKE_IPC;
				sr_thread->state = READY;
				SchedAdd(sr_thread);
				arch_reg_set(frame, 0, ERR_NOMEM);
				return;
			}
			rentry->type = HANDLE_REPLY;
			rentry->grantable = false;
			rentry->reply = rc;
			HandleEntryClaim(&current_task->owner_process->handle_table, rentry);
			ProcessTrackReplyCap(sr_thread->owner_process,
					     current_task->owner_process, slot, rc);

			arch_reg_set(frame, 0, slot);
			arch_reg_set(frame, 1, sr_thread->owner_process->pid);
			(*ArchGetFromFrame(frame, 2)) = (*ArchGetFromFrame(sr_frame, 1));
			(*ArchGetFromFrame(frame, 3)) = (*ArchGetFromFrame(sr_frame, 2));
			if (sr_thread->lmsg_buf_xfer_len > 0) {
				LmsgBufCopy(sr_thread, current_task,
					     sr_thread->lmsg_buf_xfer_len);
				(*ArchGetFromFrame(frame, 2)) = sr_thread->lmsg_buf_xfer_len;
				(*ArchGetFromFrame(frame, 3)) = 0;
				sr_thread->lmsg_buf_xfer_len = 0;
			}
		}
	} else {
		if (unlikely(timeout_ms == TIMEOUT_POLL)) {
			arch_reg_set(frame, 0, ERR_TIMEOUT);
			return;
		}


		current_task->port_wait_slot.owner = current_task;
		current_task->port_wait_slot.node.prev = NULL;
		current_task->port_wait_slot.node.next = NULL;
		current_task->ipc_state = IPC_RECEIVER;
		current_task->blocked_port = port;
		current_task->wake_reason = WAKE_NONE;
		list_add_tail(&current_task->port_wait_slot.node, &port->receiver_queue.node);
		current_task->state = BLOCKED;

		if (unlikely(timeout_ms != TIMEOUT_INFINITE)) {
			current_task->wake_deadline = ArchDeadlineFromMs(timeout_ms);
			SchedInsertSleepQueue(current_task);
		} else {
			current_task->wake_deadline = 0;
		}

#ifdef CONFIG_ZUZU_BENCH
		BENCH_END(g_bench_recv_body, bench_recv_start);
#endif
		Schedule();

#ifdef DEBUG
		if (!IsFrameNormal(frame))
			PanicBadFrame("ZuzuMsgRecv.wake", current_task->owner_process,
						 frame);
		if (timeout_ms == TIMEOUT_INFINITE && current_task->wake_reason == WAKE_TIMEOUT)
			PanicBadFrame("ZuzuMsgRecv.wake-timeout-on-infinite",
						 current_task->owner_process, frame);
#endif
		if (unlikely(timeout_ms != TIMEOUT_INFINITE &&
			     current_task->wake_reason != WAKE_TIMEOUT)) {
			SchedRemoveSleepQueue(current_task);
		}

		if (unlikely(current_task->wake_reason == WAKE_TIMEOUT)) {
			arch_reg_set(frame, 0, ERR_TIMEOUT);
		}
	}
}

void __attribute__((hot)) SysMsgCall(CpuState *frame)
{
#ifdef CONFIG_ZUZU_BENCH
	uint32_t bench_call_start = BENCH_BEGIN();
	uint32_t bs = 0;
	(void)bs;
#endif
	int handle = (int)(*ArchGetFromFrame(frame, 0));

	HandleTableEntry *entry = ValidatePortHandle(current_task->owner_process, handle, frame);
	if (unlikely(!entry))
		return;
	PortObject *port = entry->port;
	if (unlikely(!port)) {
		return;
	}

	ReplyCap *rc = KAllocReplyCap();
	if (unlikely(!rc)) {
		arch_reg_set(frame, 0, ERR_NOMEM);
		return; // caller gets clean error, never blocked
	}
	rc->caller_tid = current_task ? current_task->tid : 0;

	/* The whole point of this benchmark suite's echo-server pattern (and
	 * of the direct-handoff optimization below): the receiver is already
	 * parked in ZuzuMsgRecv waiting when the call lands. */
	if (likely(!list_empty(&port->receiver_queue))) {
#ifdef CONFIG_ZUZU_BENCH
		/* Direct-switch handoff branch only (not the block/enqueue branch
		 * below): from the port handle already resolved above, through
		 * the receiver's trap frame being written and its unblock decided. */
		uint32_t bench_start = BENCH_BEGIN();
#endif
		ListNode *receiver = list_pop_front(&port->receiver_queue);
		WaitSlot *rx_slot = container_of(receiver, WaitSlot, node);
		TaskObject *rx_thread = rx_slot->owner;
		CpuState *rx_frame = rx_thread->trap_frame;
#ifdef DEBUG
		if (!IsFrameNormal(rx_frame))
			PanicBadFrame("ZuzuMsgCall.rx", rx_thread->owner_process,
						 rx_frame);
#endif

#ifdef CONFIG_ZUZU_BENCH
		bs = BENCH_BEGIN();
#endif
		int slot = HandleTableFindFree(&rx_thread->owner_process->handle_table);
#ifdef CONFIG_ZUZU_BENCH
		BENCH_END(g_bench_slot_findfree, bs);
#endif
		if (unlikely(slot < 0)) {
			KFreeReplyCap(rc);
			list_add_tail(&rx_slot->node, &port->receiver_queue.node);
			arch_reg_set(frame, 0, ERR_NOMEM);
			return;
		}

		HandleTableEntry *rentry = HandleTableGet(&rx_thread->owner_process->handle_table, (uint32_t)slot);
		if (!rentry) {
			KFreeReplyCap(rc);
			list_add_tail(&rx_slot->node, &port->receiver_queue.node);
			arch_reg_set(frame, 0, ERR_NOMEM);
			return;
		}
		rentry->type = HANDLE_REPLY;
		rentry->grantable = false;
		rentry->reply = rc;
#ifdef CONFIG_ZUZU_BENCH
		bs = BENCH_BEGIN();
#endif
		HandleEntryClaim(&rx_thread->owner_process->handle_table, rentry);
#ifdef CONFIG_ZUZU_BENCH
		BENCH_END(g_bench_entry_claim, bs);
#endif
#ifdef CONFIG_ZUZU_BENCH
		bs = BENCH_BEGIN();
#endif
		ProcessTrackReplyCap(current_task->owner_process, rx_thread->owner_process,
				     slot, rc);
#ifdef CONFIG_ZUZU_BENCH
		BENCH_END(g_bench_track_cap, bs);
#endif

		arch_reg_set(rx_frame, 0, slot);
		arch_reg_set(rx_frame, 1, current_task->owner_process->pid);
		(*ArchGetFromFrame(rx_frame, 2)) = (*ArchGetFromFrame(frame, 1));
		(*ArchGetFromFrame(rx_frame, 3)) = (*ArchGetFromFrame(frame, 2));
		rx_thread->ipc_state = IPC_NONE;
		rx_thread->blocked_port = NULL;
		CancelTimeout(rx_thread);
		rx_thread->wake_reason = WAKE_IPC;
#ifdef CONFIG_ZUZU_BENCH
		BENCH_END(g_bench_direct_handoff, bench_start);
#endif

		current_task->state = BLOCKED;
		current_task->blocked_port = port;
		current_task->ipc_state = IPC_WAITING;

		/* Direct handoff: skip the run-queue round trip and switch straight
		 * to the receiver we just woke, as long as doing so wouldn't jump
		 * ahead of a thread that's already waiting at rx_thread's priority
		 * or higher (SchedAnyCpuTakers) -- in that case the full
		 * scheduler wouldn't have picked rx_thread next anyway, so fall back
		 * to the normal sched_add()+schedule() path. */
		if (unlikely(SchedAnyCpuTakers(rx_thread))) {
			CALL_TALLY(g_mix_call, CALLPATH_TAKERS);
#ifdef CONFIG_ZUZU_BENCH
			BENCH_END(g_bench_call_body, bench_call_start);
#endif
			rx_thread->state = READY;
			SchedAdd(rx_thread);
			Schedule();
		} else {
			CALL_TALLY(g_mix_call, CALLPATH_HANDOFF);
#ifdef CONFIG_ZUZU_BENCH
			BENCH_END(g_bench_call_body, bench_call_start);
#endif
			SchedSwitchNext(rx_thread);
		}
	} else {
		CALL_TALLY(g_mix_call, CALLPATH_NO_RECEIVER);
#ifdef CONFIG_ZUZU_BENCH
		BENCH_END(g_bench_call_body, bench_call_start);
#endif
		current_task->ipc_state = IPC_WAITING;
		current_task->blocked_port = port;
		current_task->pending_reply_cap = rc;
		current_task->port_marker = entry->marker;
		list_add_tail(&current_task->node, &port->sender_queue.node);
		current_task->state = BLOCKED;
		Schedule();
	}
}

void __attribute__((hot)) SysMsgReply(CpuState *frame)
{
#ifdef CONFIG_ZUZU_BENCH
	uint32_t bench_reply_start = BENCH_BEGIN();
	uint32_t bs = 0;
	(void)bs;
#endif
	Handle handle_idx = (Handle)(*ArchGetFromFrame(frame, 0));
	TaskObject *target_thread = NULL;
#ifdef CONFIG_ZUZU_BENCH
	bs = BENCH_BEGIN();
#endif
	HandleTableEntry *entry =
	    ValidateReplyCap(current_task->owner_process, handle_idx, &target_thread, frame);
#ifdef CONFIG_ZUZU_BENCH
	BENCH_END(g_bench_validate_replycap, bs);
#endif
	if (!entry) {
		return;
	}

	// Deliver reply into target's saved frame

	CpuState *target_frame = target_thread->trap_frame;
#ifdef DEBUG
	if (!IsFrameNormal(target_frame)) {
		PanicBadFrame("ZuzuMsgReply.target", target_thread->owner_process,
					 target_frame);
	}
#endif
	(*ArchGetFromFrame(target_frame, 0)) = 0;		      // success
	(*ArchGetFromFrame(target_frame, 1)) = (*ArchGetFromFrame(frame, 1)); // reply payload
	(*ArchGetFromFrame(target_frame, 2)) = (*ArchGetFromFrame(frame, 2));
	(*ArchGetFromFrame(target_frame, 3)) = (*ArchGetFromFrame(frame, 3));

	// Wake the caller
	target_thread->ipc_state = IPC_NONE;
	target_thread->blocked_port = NULL;
	// Cancel timeout if target had one
	CancelTimeout(target_thread);
	target_thread->wake_reason = WAKE_IPC;
	target_thread->state = READY;
	SchedAdd(target_thread);

#ifdef CONFIG_ZUZU_BENCH
	bs = BENCH_BEGIN();
#endif
	ProcessUntrackReplyCap(entry->reply);
#ifdef CONFIG_ZUZU_BENCH
	BENCH_END(g_bench_untrack_cap, bs);
#endif
	KFreeReplyCap(entry->reply);
#ifdef CONFIG_ZUZU_BENCH
	bs = BENCH_BEGIN();
#endif
	HandleEntryFree(&current_task->owner_process->handle_table, entry);
#ifdef CONFIG_ZUZU_BENCH
	BENCH_END(g_bench_entry_free, bs);
#endif
	(*ArchGetFromFrame(frame, 0)) = 0;
#ifdef CONFIG_ZUZU_BENCH
	BENCH_END(g_bench_reply_body, bench_reply_start);
#endif
}

void __attribute__((hot)) SysMsgLsend(CpuState *frame)
{
	int handle = (int)(*ArchGetFromFrame(frame, 0));
	uint32_t xlen = (*ArchGetFromFrame(frame, 1));

	HandleTableEntry *entry = ValidatePortHandle(current_task->owner_process, handle, frame);
	if (!entry)
		return;
	PortObject *port = entry->port;
	if (!port) {
		return;
	}

	/* No truncation: oversized payloads are rejected outright. */
	if (unlikely(xlen > LMSG_BUF_SIZE)) {
		arch_reg_set(frame, 0, ERR_OVERFLOW);
		return;
	}

	if (!list_empty(&port->receiver_queue)) {
		ListNode *receiver = list_pop_front(&port->receiver_queue);
		WaitSlot *rx_slot = container_of(receiver, WaitSlot, node);
		TaskObject *rx_thread = rx_slot->owner;
		CpuState *rx_frame = rx_thread->trap_frame;
#ifdef DEBUG
		if (!IsFrameNormal(rx_frame))
			PanicBadFrame("ZuzuMsgLsend.rx",
						 rx_thread->owner_process, rx_frame);
#endif
		arch_reg_set(rx_frame, 0, current_task->owner_process->pid);
		(*ArchGetFromFrame(rx_frame, 1)) = xlen;
		(*ArchGetFromFrame(rx_frame, 2)) = 0;
		(*ArchGetFromFrame(rx_frame, 3)) = 0;
		LmsgBufCopy(current_task, rx_thread, xlen);
		rx_thread->ipc_state = IPC_NONE;
		rx_thread->blocked_port = NULL;
		CancelTimeout(rx_thread);
		rx_thread->wake_reason = WAKE_IPC;
		rx_thread->state = READY;
		SchedAdd(rx_thread);
		(*ArchGetFromFrame(frame, 0)) = 0;
	} else {
		current_task->ipc_state = IPC_SENDER;
		current_task->blocked_port = port;
		current_task->port_marker = entry->marker;
		list_add_tail(&current_task->node, &port->sender_queue.node);
		current_task->lmsg_buf_xfer_len = xlen;
		current_task->state = BLOCKED;
		Schedule();
	}
}

void __attribute__((hot)) SysMsgLcall(CpuState *frame)
{
	Handle handle = (Handle)(*ArchGetFromFrame(frame, 0));
	uint32_t xlen = (*ArchGetFromFrame(frame, 1));

	HandleTableEntry *entry = ValidatePortHandle(current_task->owner_process, handle, frame);
	if (!entry)
		return;
	PortObject *port = entry->port;
	if (!port) {
		return;
	}

	/* No truncation: oversized payloads are rejected outright. */
	if (xlen > LMSG_BUF_SIZE) {
		arch_reg_set(frame, 0, ERR_OVERFLOW);
		return;
	}

	ReplyCap *rc = KAllocReplyCap();
	if (!rc) {
		arch_reg_set(frame, 0, ERR_NOMEM);
		return; // caller gets clean error, never blocked
	}
	rc->caller_tid = current_task ? current_task->tid : 0;

	if (!list_empty(&port->receiver_queue)) {
		ListNode *receiver = list_pop_front(&port->receiver_queue);
		WaitSlot *rx_slot = container_of(receiver, WaitSlot, node);
		TaskObject *rx_thread = rx_slot->owner;
		CpuState *rx_frame = rx_thread->trap_frame;
		(void)rx_frame;
#ifdef DEBUG
		if (!IsFrameNormal(rx_frame))
			PanicBadFrame("ZuzuMsgLcall.rx", rx_thread->owner_process,
						 rx_frame);
#endif
		int slot = HandleTableFindFree(&rx_thread->owner_process->handle_table);
		if (unlikely(slot < 0)) {
			KFreeReplyCap(rc);
			list_add_tail(&rx_slot->node, &port->receiver_queue.node);
			arch_reg_set(frame, 0, ERR_NOMEM);
			return;
		}

		HandleTableEntry *rentry = HandleTableGet(&rx_thread->owner_process->handle_table, (uint32_t)slot);
		if (!rentry) {
			KFreeReplyCap(rc);
			list_add_tail(&rx_slot->node, &port->receiver_queue.node);
			arch_reg_set(frame, 0, ERR_NOMEM);
			return;
		}
		rentry->type = HANDLE_REPLY;
		rentry->grantable = false;
		rentry->reply = rc;
		HandleEntryClaim(&rx_thread->owner_process->handle_table, rentry);
		ProcessTrackReplyCap(current_task->owner_process, rx_thread->owner_process,
				     slot, rc);

		arch_reg_set(rx_frame, 0, slot);
		arch_reg_set(rx_frame, 1, current_task->owner_process->pid);
		(*ArchGetFromFrame(rx_frame, 2)) = xlen;
		(*ArchGetFromFrame(rx_frame, 3)) = 0;
		LmsgBufCopy(current_task, rx_thread, xlen);
		rx_thread->ipc_state = IPC_NONE;
		rx_thread->blocked_port = NULL;
		CancelTimeout(rx_thread);
		rx_thread->wake_reason = WAKE_IPC;

		current_task->state = BLOCKED;
		current_task->blocked_port = port;
		current_task->ipc_state = IPC_WAITING;

		/* Direct handoff -- see the identical comment in SysMsgCall(). */
		if (unlikely(SchedAnyCpuTakers(rx_thread))) {
			CALL_TALLY(g_mix_lcall, CALLPATH_TAKERS);
			rx_thread->state = READY;
			SchedAdd(rx_thread);
			Schedule();
		} else {
			CALL_TALLY(g_mix_lcall, CALLPATH_HANDOFF);
			SchedSwitchNext(rx_thread);
		}
	} else {
		CALL_TALLY(g_mix_lcall, CALLPATH_NO_RECEIVER);
		current_task->ipc_state = IPC_WAITING;
		current_task->blocked_port = port;
		current_task->pending_reply_cap = rc;
		current_task->port_marker = entry->marker;
		list_add_tail(&current_task->node, &port->sender_queue.node);
		current_task->lmsg_buf_xfer_len = xlen;
		current_task->state = BLOCKED;
		Schedule();
	}
}

void __attribute__((hot)) SysMsgLreply(CpuState *frame)
{
	Handle handle_idx = (Handle)(*ArchGetFromFrame(frame, 0));
	uint32_t xlen = (*ArchGetFromFrame(frame, 1));

	/* No truncation: oversized payloads are rejected outright. */
	if (xlen > LMSG_BUF_SIZE) {
		arch_reg_set(frame, 0, ERR_OVERFLOW);
		return;
	}

	TaskObject *target_thread = NULL;
	HandleTableEntry *entry =
	    ValidateReplyCap(current_task->owner_process, handle_idx, &target_thread, frame);
	if (!entry) {
		return;
	}

	// Deliver reply into target's saved frame

	CpuState *target_frame = target_thread->trap_frame;
#ifdef DEBUG
	if (!IsFrameNormal(target_frame)) {
		PanicBadFrame("ZuzuMsgLreply.target", target_thread->owner_process,
					 target_frame);
	}
#endif
	(*ArchGetFromFrame(target_frame, 0)) = 0;    // success
	(*ArchGetFromFrame(target_frame, 1)) = xlen; // reply payload
	(*ArchGetFromFrame(target_frame, 2)) = 0;
	(*ArchGetFromFrame(target_frame, 3)) = 0;
	LmsgBufCopy(current_task, target_thread, xlen);

	// Wake the caller
	target_thread->ipc_state = IPC_NONE;
	target_thread->blocked_port = NULL;
	// Cancel timeout if target had one
	CancelTimeout(target_thread);
	target_thread->wake_reason = WAKE_IPC;
	target_thread->state = READY;
	SchedAdd(target_thread);

	ProcessUntrackReplyCap(entry->reply);
	KFreeReplyCap(entry->reply);
	HandleEntryFree(&current_task->owner_process->handle_table, entry);
	(*ArchGetFromFrame(frame, 0)) = 0;
}

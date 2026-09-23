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
		ArchSetInFrame(frame, 0, sr_thread->owner_process->pid);
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
			EphemeralReplyObject *rc = sr_thread->pending_reply_cap;
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
				ArchSetInFrame(sr_thread->trap_frame, 0, ERR_NOMEM);
				sr_thread->ipc_state = IPC_NONE;
				sr_thread->blocked_port = NULL;
				// Cancel timeout if sender had one
				CancelTimeout(sr_thread);
				sr_thread->wake_reason = WAKE_IPC;
				sr_thread->state = READY;
				SchedAdd(sr_thread);
				ArchSetInFrame(frame, 0, ERR_NOMEM);
				return;
			}

			HandleTableEntry *rentry =
			    HandleTableGet(&current_task->owner_process->handle_table, (uint32_t)slot);
			if (!rentry) {
				KFreeReplyCap(rc);
				ArchSetInFrame(sr_thread->trap_frame, 0, ERR_NOMEM);
				sr_thread->ipc_state = IPC_NONE;
				sr_thread->blocked_port = NULL;
				CancelTimeout(sr_thread);
				sr_thread->wake_reason = WAKE_IPC;
				sr_thread->state = READY;
				SchedAdd(sr_thread);
				ArchSetInFrame(frame, 0, ERR_NOMEM);
				return;
			}
			rentry->type = HANDLE_REPLY;
			rentry->grantable = false;
			rentry->reply = rc;
			HandleEntryClaim(&current_task->owner_process->handle_table, rentry);
			ProcessTrackReplyCap(sr_thread->owner_process,
					     current_task->owner_process, slot, rc);

			ArchSetInFrame(frame, 0, slot);
			ArchSetInFrame(frame, 1, sr_thread->owner_process->pid);
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
			ArchSetInFrame(frame, 0, ERR_TIMEOUT);
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
			ArchSetInFrame(frame, 0, ERR_TIMEOUT);
		}
	}
}

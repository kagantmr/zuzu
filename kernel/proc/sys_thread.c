#include "sys_thread.h"
#include "kernel/proc/thread.h"
#include "kernel/sched/sched.h"
#include "kernel/syscall/syscall.h"
#include "kernel/mm/pmm.h"
#include "process.h"
#include <arch/context.h>
#include <zuzu/tls.h>

void SysTMake(CpuState *frame)
{
	VirtAddr entry = (*arch_reg(frame, 0));
	VirtAddr usr_sp = (*arch_reg(frame, 1));
	VirtAddr arg = (*arch_reg(frame, 2));

	if (!validate_user_ptr(entry, 1)) {
		arch_reg_set(frame, 0, ERR_BADPTR);
		return;
	}
	if (!validate_user_ptr(usr_sp, 4)) {
		arch_reg_set(frame, 0, ERR_BADPTR);
		return;
	}

	ProcessObj *owner = current_thread->owner_process;
	Thread *t = ThreadCreate(owner);
	if (!t) {
		arch_reg_set(frame, 0, ERR_NOMEM);
		return;
	}

	int slot_idx = TcbSlotAlloc(owner);
	if (slot_idx < 0) {
		ThreadDestroy(t);
		arch_reg_set(frame, 0, ERR_NOMEM);
		return;
	}

    /* Lazy-back the TCB page this slot lives on, if not already mapped. */
    uint32_t tcb_page = (uint32_t)slot_idx / SLOTS_PER_PAGE;
    if (owner->tcb_page_pa[tcb_page] == 0) {
        PhysAddr new_frame = PmmAllocFrame();
        if (!new_frame) {
            TcbSlotFree(owner, slot_idx);
            ThreadDestroy(t);
            arch_reg_set(frame, 0, ERR_NOMEM);
            return;
        }
        VirtAddr page_va = owner->tcb_page_va + tcb_page * PAGE_SIZE;
        if (!VmmMapUserPage(owner->as, new_frame, page_va,
                               VM_PROT_USER | PROT_READ | PROT_WRITE)) {
            PmmFreeFrame(new_frame);
            TcbSlotFree(owner, slot_idx);
            ThreadDestroy(t);
            arch_reg_set(frame, 0, ERR_NOMEM);
            return;
        }
        memset((void *)PA_TO_VA(new_frame), 0, PAGE_SIZE);
        owner->tcb_page_pa[tcb_page] = new_frame;
    }

    ThreadData *slot = (ThreadData *)TcbSlotKVirtAddr(owner, (uint32_t)slot_idx);
	VirtAddr slot_va = TcbSlotUVirtAddr(owner, (uint32_t)slot_idx);

	slot->tid = t->tid;
	slot->pid = owner->pid;
	slot->LmsgBuf = (void *)(slot_va + offsetof(ThreadData, buf)); /* points into itself */

	t->thread_info_va = slot_va;
	t->tcb_slot = (uint8_t)slot_idx;
	t->lmsg_buf_phys_addr = TcbSlotPhysAddr(owner, (uint32_t)slot_idx) + offsetof(ThreadData, buf);

	// Build the initial kernel stack so the thread enters user mode at `entry`.
	t->kernel_sp = (uint32_t *)arch_thread_user_init(
	    (void *)t->kernel_stack_top, (uintptr_t)entry, (uintptr_t)usr_sp, USER_ELF_BASE,
	    (uint32_t)arg, 0, &t->trap_frame);
	t->state = READY;
	sched_add(t);

	arch_reg_set(frame, 0, (Tid)t->tid);
}

void SysTJoin(CpuState *frame)
{
	Tid tid = (Tid)(*arch_reg(frame, 0));
	Thread *thread = ThreadFindByTid(tid);
	if (!thread) {
		arch_reg_set(frame, 0, ERR_NOENT);
		return;
	}
	if (thread->owner_process != current_thread->owner_process) {
		arch_reg_set(frame, 0, ERR_NOPERM);
		return;
	}

	if (thread->state != ZOMBIE) {
		current_thread->owner_process->waiting_for_tid = tid;
		current_thread->state = BLOCKED;
		schedule();

		/* `process_wake_joiners` delivered the exit status into our
		 * trap frame before making us READY; do not access `thread`
		 * here since it may have been unregistered/freed by the
		 * reaper. The return value is already placed in `(*arch_reg(frame, 0))`.
		 */
		return;
	}

	/* Thread already a ZOMBIE: read the exit status (no destroy).
	 * Ownership of destruction belongs to the thread that performed
	 * the quit (tquit) and the scheduler reaper. */
	arch_reg_set(frame, 0, thread->exit_status);
}

void SysTQuit(CpuState *frame)
{
	int exit_status = (int)(*arch_reg(frame, 0));
	Thread *t = current_thread;
	ProcessObj *owner = t->owner_process;

	t->exit_status = exit_status;
	ProcessWakeJoiners(t->tid, exit_status);

	if (owner->threads.node.next == &t->process_node &&
	    t->process_node.next == &owner->threads.node) {
		// last thread, kill the process
		ProcessKill(owner, exit_status);
	} else {
		ThreadKill(t);
		// remove from process thread list NOW so process_destroy won't see it
		if (t->process_node.prev && t->process_node.next)
			list_remove(&t->process_node);
		sched_defer_destroy_thread(t);
	}

	schedule();
}

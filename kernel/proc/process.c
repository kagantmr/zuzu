#include "process.h"

#include "core/panic.h"

#include "kernel/irq/sys_irq.h"
#include "kernel/mm/alloc.h"
#include "kernel/mm/pmm.h"
#include "kernel/proc/thread.h"
#include "kernel/sched/sched.h"
#include "kernel/syspage.h"

#include <arch/cache.h>
#include <arch/context.h>
#include <arch/mmu.h>

#include <elf.h>
#include <stdint.h>
#include <string.h>

#include <zuzu/err.h>
#include <zuzu/syscall_nums.h>
#include <zuzu/tls.h>
#include <zuzu/user_layout.h>

#define LOG_FMT(fmt) "(proc) " fmt
#include "core/log.h"

uint32_t next_pid = 1;
ProcessObj *process_table[MAX_PROCESSES];

static bool elf_segment_ranges_overlap(const Elf32_Phdr *a, const Elf32_Phdr *b)
{
	uint32_t a_start = a->p_vaddr;
	uint32_t b_start = b->p_vaddr;
	uint32_t a_end = a->p_vaddr + a->p_memsz;
	uint32_t b_end = b->p_vaddr + b->p_memsz;

	if (a_end < a_start || b_end < b_start)
		return true;

	return (a_start < b_end) && (b_start < a_end);
}

/* Copy into another address space through the kernel alias of each page.
 * The target pages must already be faulted in (see fault_in_pages). */
static bool as_copy_out(AddressSpace *as, VirtAddr va, const void *src, size_t len)
{
	const uint8_t *s = src;
	while (len > 0) {
		VirtAddr page_va = va & ~(VirtAddr)(PAGE_SIZE - 1);
		PhysAddr pa = arch_mmu_translate(as->pt_root_physaddr, page_va);
		if (pa == 0)
			return false;
		size_t off = va - page_va;
		size_t n = PAGE_SIZE - off;
		if (n > len)
			n = len;
		memcpy((void *)(PA_TO_VA(pa) + off), s, n);
		s += n;
		va += n;
		len -= n;
	}
	return true;
}

ProcessObj *KernelProcessLoad(const void *elf_data, size_t elf_size, const char *name,
			      const char *argbuf, size_t argbuf_len, uint32_t argc,
			      bool leave_frozen)
{
	uint32_t elf_entry = elf_validate(elf_data, elf_size);
	if (!elf_entry)
		return NULL;

	ProcessObj *p = ProcessCreate(name);
	if (!p)
		return NULL;
	Thread *t = p->thread;
	if (!t)
		goto fail_process;
	VirtAddr stack_top = t->kernel_stack_top;

	for (int i = 0; i < elf_phdr_count(elf_data); i++) {
		Elf32_Phdr *ph_i = elf_phdr_get(elf_data, i);
		if (ph_i->p_type != PT_LOAD)
			continue;

		for (int j = i + 1; j < elf_phdr_count(elf_data); j++) {
			Elf32_Phdr *ph_j = elf_phdr_get(elf_data, j);
			if (ph_j->p_type != PT_LOAD)
				continue;

			if (elf_segment_ranges_overlap(ph_i, ph_j)) {
				KERROR("ELF load segments overlap: [%08X, %08X) and [%08X, %08X)",
				       ph_i->p_vaddr, ph_i->p_vaddr + ph_i->p_memsz, ph_j->p_vaddr,
				       ph_j->p_vaddr + ph_j->p_memsz);
				goto fail_kstack;
			}
		}
	}

	for (int i = 0; i < elf_phdr_count(elf_data); i++) {
		Elf32_Phdr *ph = elf_phdr_get(elf_data, i);
		if (ph->p_type == PT_LOAD) {
			if (ph->p_offset + ph->p_filesz > elf_size) {
				goto fail_kstack;
			}
			/* [filesz, memsz) is BSS: zero by definition, with no file
			 * content behind it. Only [0, page_align_up(filesz)) needs an
			 * eager alloc+copy; the rest is registered as anon and faults
			 * in lazily via vmm_fault_page(), same as the stack reserve. */
			size_t file_pages = (ph->p_filesz + PAGE_SIZE - 1) / PAGE_SIZE;
			size_t mem_pages = (ph->p_memsz + PAGE_SIZE - 1) / PAGE_SIZE;

			uintptr_t *segment_pages = NULL;
			if (file_pages > 0) {
				segment_pages = kmalloc(file_pages * sizeof(uintptr_t));
				if (!segment_pages)
					goto fail_kstack;
			}

			uint32_t prot = 0;
			if (ph->p_flags & PF_R)
				prot |= PROT_READ;
			if (ph->p_flags & PF_W)
				prot |= PROT_WRITE;
			if (ph->p_flags & PF_X)
				prot |= PROT_EXEC;

			for (uint32_t page = 0; page < file_pages; page++) {
				uintptr_t page_pa = PmmAllocFrame();
				if (!page_pa) {
					for (uint32_t j = 0; j < page; j++) {
						uintptr_t orphan_va = ph->p_vaddr + j * PAGE_SIZE;
						VmmUnmapRange(p->as, orphan_va, PAGE_SIZE);
						PmmFreeFrame(segment_pages[j]);
					}
					kfree(segment_pages);
					goto fail_kstack;
				}

				segment_pages[page] = page_pa;

				/* Every page here is < file_pages, so file_offset < p_filesz
				 * always holds; the page containing p_filesz (the boundary
				 * page) is part file content, part BSS, so the tail past
				 * p_filesz must be explicitly zeroed - PmmAllocPage() can
				 * return a recycled frame with arbitrary contents. */
				VirtAddr file_offset = page * PAGE_SIZE;
				size_t bytes_to_copy = ph->p_filesz - file_offset;
				if (bytes_to_copy > PAGE_SIZE)
					bytes_to_copy = PAGE_SIZE;

				memcpy((void *)PA_TO_VA(page_pa),
				       (const uint8_t *)elf_data + ph->p_offset + file_offset,
				       bytes_to_copy);

				if (bytes_to_copy < PAGE_SIZE) {
					memset((uint8_t *)PA_TO_VA(page_pa) + bytes_to_copy, 0,
					       PAGE_SIZE - bytes_to_copy);
				}

				VirtAddr va = ph->p_vaddr + page * PAGE_SIZE;
				if (!VmmMapUserPage(p->as, page_pa, va, prot)) {
					PmmFreeFrame(page_pa);
					for (uint32_t j = 0; j < page; j++) {
						VirtAddr orphan_va = ph->p_vaddr + j * PAGE_SIZE;
						VmmUnmapRange(p->as, orphan_va, PAGE_SIZE);
						PmmFreeFrame(segment_pages[j]);
					}
					kfree(segment_pages);
					goto fail_kstack;
				}

				arch_cache_flush_code_range((uintptr_t)PA_TO_VA(page_pa),
							    PAGE_SIZE);
			}

			if (file_pages > 0) {
				VirtMemRegion seg_region = {
				    .vaddr_start = ph->p_vaddr,
				    .size = file_pages * PAGE_SIZE,
				    .prot = prot | VM_PROT_USER,
				    .memtype = VM_MEM_NORMAL,
				    .owner = VM_OWNER_ANON,
				    .flags = VM_FLAG_NONE,
				};
				if (!VmmAddRegion(p->as, &seg_region)) {
					KERROR("Failed to add ELF segment region at VA %08X",
					       ph->p_vaddr);
					for (uint32_t j = 0; j < file_pages; j++) {
						VirtAddr orphan_va = ph->p_vaddr + j * PAGE_SIZE;
						VmmUnmapRange(p->as, orphan_va, PAGE_SIZE);
						PmmFreeFrame(segment_pages[j]);
					}
					kfree(segment_pages);
					goto fail_kstack;
				}

				kfree(segment_pages);
			}

			if (mem_pages > file_pages) {
				VirtMemRegion bss_region = {
				    .vaddr_start = ph->p_vaddr + file_pages * PAGE_SIZE,
				    .size = (mem_pages - file_pages) * PAGE_SIZE,
				    .prot = prot | VM_PROT_USER,
				    .memtype = VM_MEM_NORMAL,
				    .owner = VM_OWNER_ANON,
				    .flags = VM_FLAG_NONE,
				};
				if (!VmmAddRegion(p->as, &bss_region)) {
					KERROR("Failed to add BSS region at VA %08X",
					       bss_region.vaddr_start);
					goto fail_kstack;
				}
			}
		}
	}

	/* Stack region + guard were reserved by process_create; pages fault
	 * in on demand. */
	const VirtAddr user_stack_base = USER_STACK_BASE;

	VirtAddr sp = USR_SP;
	VirtAddr argv_va = 0;

	if ((argc > 0) != (argbuf && argbuf_len > 0)) {
		KERROR("Invalid argv payload: argc=%u argbuf_len=%u", argc, (unsigned)argbuf_len);
		goto fail_kstack;
	}

	if (argbuf && argbuf_len > 0 && argc > 0) {
		if (((const char *)argbuf)[argbuf_len - 1] != '\0') {
			KERROR("Invalid argv payload: missing trailing NUL");
			goto fail_kstack;
		}

		size_t nul_count = 0;
		for (size_t i = 0; i < argbuf_len; i++) {
			if (((const char *)argbuf)[i] == '\0') {
				nul_count++;
			}
		}
		if (nul_count < argc) {
			KERROR("Invalid argv payload: argc exceeds NUL-delimited strings");
			goto fail_kstack;
		}

		size_t argv_slots = (size_t)argc + 1u;
		if (argv_slots <= (size_t)argc) {
			KERROR("Invalid argv payload: argc too large");
			goto fail_kstack;
		}

		size_t argv_bytes = argv_slots * sizeof(uint32_t);
		if (argv_bytes / sizeof(uint32_t) != argv_slots) {
			KERROR("Invalid argv payload: argv bytes overflow");
			goto fail_kstack;
		}
		VirtAddr check_sp = USR_SP;

		if (argbuf_len > (size_t)(check_sp - user_stack_base)) {
			KERROR("argv payload does not fit user stack");
			goto fail_kstack;
		}

		check_sp -= argbuf_len;
		check_sp &= ~((uintptr_t)3u);

		if (argv_bytes > (size_t)(check_sp - user_stack_base)) {
			KERROR("argv pointer array does not fit user stack");
			goto fail_kstack;
		}

		check_sp -= argv_bytes;
		check_sp &= ~((uintptr_t)7u);

		if (check_sp < user_stack_base) {
			KERROR("argv layout underflowed user stack");
			goto fail_kstack;
		}

		sp -= argbuf_len;
		sp &= ~3u;
		VirtAddr strings_va = sp;

		sp -= (argc + 1) * sizeof(uint32_t);
		sp &= ~7u;
		argv_va = sp;

		/* Fault in the stack pages the argv block spans, then write them
		 * through the kernel alias (the target AS is not active here). */
		if (!VmmCheckUserFault(p->as, argv_va, (size_t)(USR_SP - argv_va), true)) {
			KERROR("failed to fault in argv stack pages");
			goto fail_kstack;
		}

		if (!as_copy_out(p->as, strings_va, argbuf, argbuf_len))
			goto fail_kstack;

		/* String offsets in the target stack mirror offsets in argbuf. */
		VirtAddr str_va = strings_va;
		const char *str_src = argbuf;
		for (uint32_t a = 0; a <= argc; a++) {
			uint32_t slot = (a < argc) ? (uint32_t)str_va : 0;
			if (!as_copy_out(p->as, argv_va + a * sizeof(uint32_t), &slot,
					 sizeof(slot)))
				goto fail_kstack;
			if (a < argc) {
				size_t l = strlen(str_src) + 1;
				str_va += l;
				str_src += l;
			}
		}
	}

	if (!leave_frozen) {
		t->kernel_sp = (uint32_t *)arch_thread_user_init(
		    (void *)stack_top, (uintptr_t)elf_entry, (uintptr_t)sp, USER_ELF_BASE, argc,
		    (uint32_t)(VirtAddr)argv_va, &t->trap_frame);
		t->state = READY;
	}
	/* leave_frozen: thread stays FROZEN (ThreadCreate's default) with no
	 * trap frame set up yet. The caller is expected to SysKickstart this
	 * process later, which performs the deferred arch_thread_user_init
	 * call with the entry/sp it supplies at that time. */

	KTRACE("process create: pid=%u name=%s tid=%u owner_thread=%p as=%p", p->pid, p->name,
	       p->thread ? p->thread->tid : 0, (void *)p->thread, (void *)p->as);
	return p;

fail_kstack:
	if (process_table[p->pid % MAX_PROCESSES] == p)
		process_table[p->pid % MAX_PROCESSES] = NULL;

	if (p->as)
		arch_mmu_free_user_pages(p->as);
	AddrspaceDestroy(p->as);
	memset(p->tcb_page_pa, 0, sizeof(p->tcb_page_pa));
	handle_vec_destroy(&p->handle_table);
	ThreadDestroy(t);
fail_process:
	kfree(p);
	return NULL;
}

void ProcessTrackReplyCap(ProcessObj *restrict caller, ProcessObj *restrict holder,
			  Handle holder_slot, ReplyCap *restrict rc)
{
	rc->holder_pid = holder ? holder->pid : 0;
	rc->holder_slot = holder_slot;
	rc->caller_link.prev = NULL;
	rc->caller_link.next = NULL;
	list_add_tail(&rc->caller_link, &caller->outstanding_replies.node);
}

ProcessObj *ProcessCreate(const char *name)
{
	ProcessObj *p = kmalloc(sizeof(ProcessObj));
	if (!p)
		return NULL;
	memset(p, 0, sizeof(ProcessObj));

	list_init(&p->outstanding_replies);
	list_init(&p->threads);
	list_init(&p->children);

	Thread *t = ThreadCreate(p);
	if (!t)
		goto fail_process;
	p->thread = t;

	if (!handle_vec_init(&p->handle_table))
		goto fail_process;

	p->as = AddrspaceCreate(ADDRSPACE_USER);
	if (!p->as)
		goto fail_handles;

	// map syspage into user space
	if (!VmmMapUserPage(p->as, SyspagePhysAddr(), USER_SYSPAGE_VA, PROT_READ))
		goto fail_kstack;

	VirtMemRegion sys_region = {
	    .vaddr_start = USER_SYSPAGE_VA,
	    .size = PAGE_SIZE,
	    .prot = PROT_READ | VM_PROT_USER,
	    .memtype = VM_MEM_NORMAL,
	    .owner = VM_OWNER_SHARED,
	    .flags = VM_FLAG_PINNED | VM_FLAG_GUARD,
	};
	if (!VmmAddRegion(p->as, &sys_region))
		goto fail_kstack;

	/* Initialize per-process mmap bump pointer before allocating the
	 * TCB mapping so we can place the TCB page at the process's
	 * `mmap_va_next` value and then advance it. */
	p->device_va_next = USER_DEVICE_BASE;
	p->mmap_va_next = USER_MMAP_BASE;

	PhysAddr tcb_page0_phys_addr = PmmAllocFrame();
	if (!tcb_page0_phys_addr)
		goto fail_kstack;
	p->tcb_page_pa[0] = tcb_page0_phys_addr;
	/* Map the TCB page into the user mmap area at the process's bump
	 * pointer so userspace can read its per-thread slot. */
	VirtAddr tcb_user_va = p->mmap_va_next;
	if (!VmmMapUserPage(p->as, tcb_page0_phys_addr, tcb_user_va,
			       VM_PROT_USER | PROT_READ | PROT_WRITE))
		goto fail_kstack;

	VirtMemRegion tcb_region = {
	    .vaddr_start = tcb_user_va,
	    .size = MAX_TCB_PAGES * PAGE_SIZE,
	    .prot = PROT_READ | PROT_WRITE | VM_PROT_USER,
	    .owner = VM_OWNER_ANON, // GUARD dropped so pages 1..N demand-back; PINNED still blocks user unmap.
	    .flags = VM_FLAG_PINNED,
	};
	if (!VmmAddRegion(p->as, &tcb_region))
		goto fail_kstack;

	p->tcb_page_va = tcb_user_va; /* user-visible VA */

	/* Advance bump pointer to reserve the TCB page */
	p->mmap_va_next += MAX_TCB_PAGES * PAGE_SIZE;

	/* Reserve the whole stack window as a demand-paged anon region: no
	 * physical pages up front, the data-abort handler faults them in as
	 * the stack grows down. Guard page below catches overflow. */
	VirtMemRegion stack_region = {
	    .vaddr_start = USER_STACK_BASE,
	    .size = USER_STACK_TOP - USER_STACK_BASE,
	    .prot = PROT_READ | PROT_WRITE | VM_PROT_USER,
	    .memtype = VM_MEM_NORMAL,
	    .owner = VM_OWNER_ANON,
	    .flags = VM_FLAG_NONE,
	};
	if (!VmmAddRegion(p->as, &stack_region))
		goto fail_kstack;

	VirtMemRegion stack_guard = {
	    .vaddr_start = USER_STACK_GUARD_VA,
	    .size = PAGE_SIZE,
	    .prot = 0,
	    .memtype = VM_MEM_NORMAL,
	    .owner = VM_OWNER_NONE,
	    .flags = VM_FLAG_GUARD,
	};
	if (!VmmAddRegion(p->as, &stack_guard))
		goto fail_kstack;

	/* Initialize the page via the kernel alias (kernel VA). The main
	 * thread takes its slot through the same bitmap allocator as
	 * tmake; its lmsg buffer lives inside the slot itself. */
	memset((void *)PA_TO_VA(tcb_page0_phys_addr), 0, PAGE_SIZE);
	memset(p->tcb_slot_bitmap, 0, sizeof(p->tcb_slot_bitmap));
	int tcb_slot_idx = TcbSlotAlloc(p);
	if (tcb_slot_idx < 0)
		goto fail_kstack;
	ThreadData *tcb0 = (ThreadData *)TcbSlotKVirtAddr(p, tcb_slot_idx);
	VirtAddr tcb0_va = TcbSlotUVirtAddr(p, tcb_slot_idx);
	tcb0->LmsgBuf = (void *)(tcb0_va + offsetof(ThreadData, buf));
	tcb0->tid = t->tid;
	t->thread_info_va = tcb0_va;
	t->tcb_slot = (uint8_t)tcb_slot_idx;
	t->lmsg_buf_phys_addr = TcbSlotPhysAddr(p, tcb_slot_idx) + offsetof(ThreadData, buf);

	/* `device_va_next` and `mmap_va_next` were initialized earlier. */
	p->parent_pid = 0;
	t->priority = 1;
	t->time_slice = 5;
	t->ticks_remaining = t->time_slice;
	p->flags = 0;

	if (name) {
		const char *short_name = name;
		for (const char *p = name; *p; p++) {
			if (*p == '/')
				short_name = p + 1;
		}
		strncpy(p->name, short_name, sizeof(p->name) - 1);
	}

	Pid start = next_pid % MAX_PROCESSES;
	Tid slot = start;
	do {
		if (process_table[slot] == NULL)
			break;
		next_pid++;
		slot = next_pid % MAX_PROCESSES;
	} while (slot != start);

	if (process_table[slot] != NULL)
		goto fail_kstack;

	p->pid = next_pid++;
	process_table[slot] = p;
	tcb0->pid = p->pid;
	return p;

fail_kstack:
	if (p->as)
		arch_mmu_free_user_pages(p->as);
	AddrspaceDestroy(p->as);
	memset(p->tcb_page_pa, 0, sizeof(p->tcb_page_pa));
fail_handles:
	handle_vec_destroy(&p->handle_table);
	ThreadDestroy(t);
fail_process:
	kfree(p);
	return NULL;
}

void ProcessUntrackReplyCap(ReplyCap *rc)
{
	if (!rc)
		return;

	if (rc->caller_link.prev && rc->caller_link.next)
		list_remove(&rc->caller_link);

	rc->caller_link.prev = NULL;
	rc->caller_link.next = NULL;
	rc->caller_tid = 0;
	rc->holder_pid = 0;
	rc->holder_slot = 0;
}

static void process_revoke_outstanding_reply_caps(ProcessObj *caller)
{
	while (!list_empty(&caller->outstanding_replies)) {
		ListNode *node = list_pop_front(&caller->outstanding_replies);
		ReplyCap *rc = container_of(node, ReplyCap, caller_link);

		ProcessObj *holder = ProcessFindByPid(rc->holder_pid);
		if (holder) {
			HandleEntry *entry = handle_vec_get(&holder->handle_table, rc->holder_slot);

			if (entry && entry->type == HANDLE_REPLY && entry->reply == rc) {
				entry->reply = NULL;
				entry->grantable = false;
				entry->type = HANDLE_FREE;
			}
		}

		rc->caller_link.prev = NULL;
		rc->caller_link.next = NULL;
		rc->caller_tid = 0;
		rc->holder_pid = 0;
		rc->holder_slot = 0;
		kfree_reply_cap(rc);
	}
}

ProcessObj *ProcessFindByPid(Pid pid)
{
	uint32_t slot = pid % MAX_PROCESSES;
	ProcessObj *p = process_table[slot];
	if (p && p->pid == pid)
		return p;
	return NULL;
}

void ProcessSetParent(ProcessObj *child, ProcessObj *parent)
{
	if (!child)
		return;

	if (child->sibling_node.prev && child->sibling_node.next)
		list_remove(&child->sibling_node);

	child->parent_pid = parent ? parent->pid : 0;

	if (parent)
		list_add_tail(&child->sibling_node, &parent->children.node);
}

ProcessObj *ProcessFindChildFromPid(ProcessObj *parent, Pid pid)
{
	if (!parent)
		return NULL;

	ListNode *node = parent->children.node.next;
	while (node != &parent->children.node) {
		ProcessObj *child = container_of(node, ProcessObj, sibling_node);
		if (child->pid == pid)
			return child;
		node = node->next;
	}

	return NULL;
}

ProcessObj *ProcessFindZombieChild(ProcessObj *parent)
{
	if (!parent)
		return NULL;

	ListNode *node = parent->children.node.next;
	while (node != &parent->children.node) {
		ProcessObj *child = container_of(node, ProcessObj, sibling_node);
		if (child->thread->state == ZOMBIE)
			return child;
		node = node->next;
	}

	return NULL;
}

void ProcessWakeJoiners(Tid tid, int32_t exit_status)
{
	for (uint32_t slot = 0; slot < MAX_PROCESSES; slot++) {
		ProcessObj *joiner = process_table[slot];
		if (!joiner || joiner->waiting_for_tid != tid)
			continue;

		joiner->waiting_for_tid = 0;
		if (joiner->thread) {
			joiner->thread->wake_reason = WAKE_IPC;
			joiner->thread->state = READY;
			if (joiner->thread->trap_frame)
				(*arch_reg(joiner->thread->trap_frame, 0)) = (uint32_t)exit_status;
			sched_add(joiner->thread);
		}
	}
}

static const char *fatal_reason_str(int reason)
{
	switch (reason) {
	case FATAL_KERNEL_OUTDATED:
		return "kernel and sysd version don't match";
	default:
		return "no reason specified";
	}
}

void ProcessKill(ProcessObj *p, const int exit_status)
{
	if (!p)
		return;

	if (p->flags & (PROC_FLAG_INIT | PROC_FLAG_DEVMGR)) {
		if ((exit_status & FATAL_TAG_MASK) == FATAL_TAG) {
			/* deliberate fatal exit carrying a reason code */
			panic("critical process '%s' (pid %u) exited: %s", p->name,
			      (unsigned)p->pid, fatal_reason_str(exit_status & FATAL_REASON_MASK));
		} else {
			/* unexpected death, or exit with no reason */
			panic("critical process '%s' (pid %u) died unexpectedly (status %d)",
			      p->name, (unsigned)p->pid, exit_status);
		}
	}

	ListNode *thread_node = p->threads.node.next;
	while (thread_node != &p->threads.node) {
		ListNode *next_thread = thread_node->next;
		Thread *thread = container_of(thread_node, Thread, process_node);
		thread->exit_status = exit_status;

		// remove from run queue / sleep queue / IPC queue
		if (thread->node.prev && thread->node.next)
			list_remove(&thread->node);
		if (thread->timeout_node.prev && thread->timeout_node.next)
			list_remove(&thread->timeout_node);

		ThreadKill(thread); // state = ZOMBIE
		thread_node = next_thread;
	}
	p->exit_status = exit_status;

	// Clean up handle table
	for (uint32_t i = 0; i < p->handle_table.cap; i++) {
		HandleEntry *entry = handle_vec_get(&p->handle_table, i);
		if (!entry)
			break;

		if (entry->type == HANDLE_PORT) {
			Port *port = entry->port;
			if (port && port->owner_pid == p->pid && port->alive) {
				port->alive = false;
				// Wake blocked waiters with ERR_DEAD
				while (!list_empty(&port->sender_queue)) {
					ListNode *n = list_pop_front(&port->sender_queue);
					Thread *thread = container_of(n, Thread, node);
					thread->ipc_state = IPC_NONE;
					thread->blocked_port = NULL;
					thread->wake_reason = WAKE_IPC;
					if (thread->trap_frame)
						(*arch_reg(thread->trap_frame, 0)) = ERR_DEAD;
					thread->state = READY;
					sched_add(thread);
				}
				while (!list_empty(&port->receiver_queue)) {
					ListNode *n = list_pop_front(&port->receiver_queue);
					ThreadWaitSlot *slot =
					    container_of(n, ThreadWaitSlot, node);
					Thread *thread = slot->owner;
					if (thread->waitany_port_wait_active) {
						ThreadWaitanyClearWaits(thread);
						ThreadWaitanyClearPortWaits(thread);
					} else {
						thread->ipc_state = IPC_NONE;
						thread->blocked_port = NULL;
					}
					if (thread->wake_tick != 0 && thread->timeout_node.prev &&
					    thread->timeout_node.next)
						list_remove(&thread->timeout_node);
					thread->wake_tick = 0;
					thread->wake_reason = WAKE_IPC;
					if (thread->trap_frame)
						(*arch_reg(thread->trap_frame, 0)) = ERR_DEAD;
					thread->state = READY;
					sched_add(thread);
				}
			}
			if (port) {
				if (port->ref_count > 0)
					port->ref_count--;
				if (port->ref_count == 0)
					kfree_portobj(port);
			}
			entry->port = NULL;
			entry->grantable = false;
			entry->type = HANDLE_FREE;
		} else if (entry->type == HANDLE_DEVICE) {
			if (entry->dev) {
				if (entry->dev->ref_count > 0)
					entry->dev->ref_count--;
				if (entry->dev->ref_count == 0)
					kfree_device_cap(entry->dev);
			}
			entry->dev = NULL;
			entry->mapped_va = 0;
			entry->grantable = false;
			entry->type = HANDLE_FREE;
		} else if (entry->type == HANDLE_SHM) {
			ShmCap *shm = entry->shm;
			// Every shm handle holds one reference (create/grant), regardless
			// of whether it is currently mapped. Tear down the mapping if any,
			// then drop this handle's reference (frees the object at zero).
			if (shm) {
				if (entry->mapped_va != 0)
					VmmRemoveRegion(p->as, entry->mapped_va,
							  shm->page_count * PAGE_SIZE);
				ShmemDropReference(shm);
			}
			entry->shm = NULL;
			entry->mapped_va = 0;
			entry->grantable = false;
			entry->type = HANDLE_FREE;
		} else if (entry->type == HANDLE_REPLY) {
			ReplyCap *rc = entry->reply;
			Thread *caller_thread = ThreadFindByTid(rc ? rc->caller_tid : 0);

			if (caller_thread && caller_thread->ipc_state == IPC_WAITING) {
				caller_thread->ipc_state = IPC_NONE;
				caller_thread->blocked_port = NULL;
				caller_thread->wake_reason = WAKE_IPC;
				if (caller_thread->trap_frame)
					(*arch_reg(caller_thread->trap_frame, 0)) = ERR_DEAD;
				caller_thread->state = READY;
				sched_add(caller_thread);
			}

			if (rc) {
				ProcessUntrackReplyCap(rc);
				kfree_reply_cap(rc);
			}

			entry->reply = NULL;
			entry->grantable = false;
			entry->type = HANDLE_FREE;
		} else if (entry->type == HANDLE_NTFN) {
			Ntfn *ntfn = entry->ntfn;
			if (ntfn && ntfn->owner_pid == p->pid && ntfn->alive) {
				ntfn->alive = false;
				while (!list_empty(&ntfn->wait_queue)) {
					ListNode *n = list_pop_front(&ntfn->wait_queue);
					ThreadWaitSlot *slot =
					    container_of(n, ThreadWaitSlot, node);
					Thread *thread = slot->owner;
					if (thread->trap_frame)
						(*arch_reg(thread->trap_frame, 0)) = ERR_DEAD;
					ThreadWaitanyClearWaits(thread);
					if (thread->wake_tick != 0 && thread->timeout_node.prev &&
					    thread->timeout_node.next)
						list_remove(&thread->timeout_node);
					thread->wake_tick = 0;
					thread->state = READY;
					thread->wake_reason = WAKE_IPC;
					thread->blocked_port = NULL;
					thread->ipc_state = IPC_NONE;
					sched_add(thread);
				}
			}
			if (ntfn) {
				if (ntfn->ref_count > 0)
					ntfn->ref_count--;
				if (ntfn->ref_count == 0)
					kfree(ntfn);
			}
			entry->ntfn = NULL;
			entry->grantable = false;
			entry->type = HANDLE_FREE;
		} else if (entry->type == HANDLE_TASK) {
			// No special cleanup needed for task handles since they don't have kernel
			// objects associated with them
			entry->task = NULL;
			entry->mapped_va = 0;
			entry->grantable = false;
			entry->type = HANDLE_FREE;
		}
	}

	process_revoke_outstanding_reply_caps(p);

	ProcessObj *init_proc = ProcessFindByPid(1);
	ListNode *child_node = p->children.node.next;
	while (child_node != &p->children.node) {
		ListNode *next = child_node->next;
		ProcessObj *child = container_of(child_node, ProcessObj, sibling_node);
		ProcessSetParent(child, init_proc);
		child_node = next;
	}

	ProcessObj *parent = ProcessFindByPid(p->parent_pid);
	if (parent && parent->thread && parent->thread->state == BLOCKED &&
	    (parent->waiting_for == p->pid || parent->waiting_for == -1)) {
		parent->thread->state = READY;
		parent->waiting_for = 0;
		sched_add(parent->thread);
	} else if (parent && parent->thread) {
		/* Parent is alive but not blocked in wait() right now.
		 * Leave p as a zombie on the child list; a later wait() reaps it. */
	} else {
		/* No parent left to reap us. Destroy now. */
		sched_defer_destroy(p);
	}
}

void ProcessDestroy(ProcessObj *p)
{
	if (!p)
		return;

	// KDEBUG("process_destroy: pid=%u current_tid=%u", p->pid, current_thread ?
	// current_thread->tid : 0);

	IrqReleaseAll(p);
	if (p->node.prev && p->node.next)
		list_remove(&p->node);
	if (p->sibling_node.prev && p->sibling_node.next)
		list_remove(&p->sibling_node);
	if (p->destroy_node.prev && p->destroy_node.next)
		list_remove(&p->destroy_node);
	if (p->timeout_node.prev && p->timeout_node.next)
		list_remove(&p->timeout_node);
	while (!list_empty(&p->threads)) {
		ListNode *node = p->threads.node.next;
		Thread *thread = container_of(node, Thread, process_node);
		ThreadDestroy(thread);
	}
	/* Drop shm handle references still live here. The normal exit path
	 * (process_kill) already cleared these, so this is a no-op there; it
	 * catches the direct-destroy path (SysPKill) that bypasses process_kill
	 * and would otherwise leak the shm object and its pages. Runs before
	 * as_destroy so the address space is still valid for unmapping. */
	for (uint32_t i = 0; i < p->handle_table.cap; i++) {
		HandleEntry *entry = handle_vec_get(&p->handle_table, i);
		if (!entry)
			break;
		if (entry->type == HANDLE_SHM && entry->shm) {
			if (p->as && entry->mapped_va != 0)
				VmmRemoveRegion(p->as, entry->mapped_va,
						  entry->shm->page_count * PAGE_SIZE);
			ShmemDropReference(entry->shm);
			entry->shm = NULL;
			entry->mapped_va = 0;
			entry->type = HANDLE_FREE;
		}
	}
	if (p->as) {
		arch_mmu_free_user_pages(p->as);
		AddrspaceDestroy(p->as);
	}
	handle_vec_destroy(&p->handle_table);
	process_table[p->pid % MAX_PROCESSES] = NULL;
	kfree(p);
}

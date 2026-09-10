#ifndef KERNEL_PROC_PROCESS_H
#define KERNEL_PROC_PROCESS_H

#include "thread.h"

#include "kernel/ipc/handle.h"
#include "kernel/ipc/port.h"
#include "kernel/mm/vmm.h"

#include <arch/regs.h>

#include <bitmap.h>
#include <list.h>
#include <stddef.h>
#include <stdint.h>

#include <zuzu/tls.h>
#include <zuzu/types.h>
#include <zuzu/err.h>

#define MAX_PROCESSES 512

#define PROC_FLAG_INIT (1 << 0)	  // PID 1
#define PROC_FLAG_DEVMGR (1 << 1) // hardware authority

extern void process_entry_trampoline(void);

typedef struct process {
	Pid pid, parent_pid;
	AddressSpace *as;
	ListNode node; // embedded, not pointers
	ListNode destroy_node;
	ListNode timeout_node;
	Err exit_status;
	Pid waiting_for;
	char name[32];		 // PROCESS name
	VirtAddr device_va_next; // initialized to USER_DEVICE_BASE in process_create
	VirtAddr mmap_va_next;	 // initialized to USER_MMAP_BASE in process_create
	ListHead outstanding_replies;
	HandleTable handle_table;
	uint32_t flags;
	Thread *thread;
	Tid waiting_for_tid;
	ListHead threads;
	ListHead children;
	ListNode sibling_node;
    PhysAddr tcb_page_pa[MAX_TCB_PAGES];    /* 37 entries */
    VirtAddr tcb_page_va;                   /* singular: contiguous window base */
    uint32_t tcb_slot_bitmap[BITMAP_WORDS(256)]; /* 256 bits */
    Label label;
} ProcessObj;

_Static_assert(TCB_MAX_SLOTS <= 256, "tcb_slot_bitmap is 256 bits wide");

static inline int TcbSlotAlloc(ProcessObj *p)
{
    int slot = BitmapFindFirstZero(p->tcb_slot_bitmap, TCB_MAX_SLOTS);
    if (slot < 0)
        return -1;
    BitmapSet(p->tcb_slot_bitmap, (size_t)slot);
    return slot;
}

static inline void TcbSlotFree(ProcessObj *p, int slot)
{
    BitmapClr(p->tcb_slot_bitmap, (size_t)slot);
}

/* Physical base of the frame backing this slot's TCB page. */
static inline PhysAddr TcbSlotPhysAddr(ProcessObj *p, uint32_t slot)
{
    return p->tcb_page_pa[slot / SLOTS_PER_PAGE]
         + ((slot % SLOTS_PER_PAGE) * TCB_SLOT_SIZE);
}

/* Kernel VA of this slot. */
static inline VirtAddr TcbSlotKVirtAddr(ProcessObj *p, uint32_t slot)
{
    return PA_TO_VA(p->tcb_page_pa[slot / SLOTS_PER_PAGE])
         + ((slot % SLOTS_PER_PAGE) * TCB_SLOT_SIZE);
}

/* User VA of this slot. */
static inline VirtAddr TcbSlotUVirtAddr(ProcessObj *p, uint32_t slot)
{
    return p->tcb_page_va + ((slot / SLOTS_PER_PAGE) * PAGE_SIZE)
         + ((slot % SLOTS_PER_PAGE) * TCB_SLOT_SIZE);
}

void ProcessDestroy(ProcessObj *process);
ProcessObj *ProcessFindByPid(Pid pid);
ProcessObj *ProcessCreate(const char *name);
void ProcessWakeJoiners(Tid tid, Err exit_status);
ProcessObj *KernelProcessLoad(const void *elf_data, size_t elf_size, const char *name,
			      const char *argbuf, size_t argbuf_len, uint32_t argc,
			      bool leave_frozen);
void ProcessKill(ProcessObj *p, int exit_status);
void ProcessSetParent(ProcessObj *child, ProcessObj *parent);
ProcessObj *ProcessFindChildFromPid(ProcessObj *parent, Pid pid);
ProcessObj *ProcessFindZombieChild(ProcessObj *parent);
/* caller/holder/rc are always three distinct objects (never the same
 * process, never a process aliased with the reply-cap slab object). */
void ProcessTrackReplyCap(ProcessObj *restrict caller, ProcessObj *restrict holder,
			  Handle holder_slot, ReplyCap *restrict rc);
void ProcessUntrackReplyCap(ReplyCap *rc);

#endif // KERNEL_PROC_PROCESS_H

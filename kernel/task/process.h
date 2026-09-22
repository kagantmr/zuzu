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


_Static_assert(TCB_MAX_SLOTS <= 256, "tcb_slot_bitmap is 256 bits wide");

static inline int TcbSlotAlloc(SpaceObject *p)
{
    int slot = BitmapFindFirstZero(p->tcb_slot_bitmap, TCB_MAX_SLOTS);
    if (slot < 0)
        return -1;
    BitmapSet(p->tcb_slot_bitmap, (size_t)slot);
    return slot;
}

static inline void TcbSlotFree(SpaceObject *p, int slot)
{
    BitmapClr(p->tcb_slot_bitmap, (size_t)slot);
}

/* Physical base of the frame backing this slot's TCB page. */
static inline PhysAddr TcbSlotPhysAddr(SpaceObject *p, uint32_t slot)
{
    return p->tcb_page_pa[slot / SLOTS_PER_PAGE]
         + ((slot % SLOTS_PER_PAGE) * TCB_SLOT_SIZE);
}

/* Kernel VA of this slot. */
static inline VirtAddr TcbSlotKVirtAddr(SpaceObject *p, uint32_t slot)
{
    return PA_TO_VA(p->tcb_page_pa[slot / SLOTS_PER_PAGE])
         + ((slot % SLOTS_PER_PAGE) * TCB_SLOT_SIZE);
}

/* User VA of this slot. */
static inline VirtAddr TcbSlotUVirtAddr(SpaceObject *p, uint32_t slot)
{
    return p->tcb_page_va + ((slot / SLOTS_PER_PAGE) * PAGE_SIZE)
         + ((slot % SLOTS_PER_PAGE) * TCB_SLOT_SIZE);
}

void ProcessDestroy(SpaceObject *process);
SpaceObject *ProcessFindByPid(Pid pid);
SpaceObject *ProcessCreate(const char *name);
SpaceObject *KernelProcessLoad(const void *elf_data, size_t elf_size, const char *name,
			      const char *argbuf, size_t argbuf_len, uint32_t argc,
			      bool leave_frozen);
void ProcessKill(SpaceObject *p, int exit_status);
void ProcessSetParent(SpaceObject *child, SpaceObject *parent);
SpaceObject *ProcessFindChildFromPid(SpaceObject *parent, Pid pid);
SpaceObject *ProcessFindZombieChild(SpaceObject *parent);
/* caller/holder/rc are always three distinct objects (never the same
 * process, never a process aliased with the reply-cap slab object). */
void ProcessTrackReplyCap(SpaceObject *restrict caller, SpaceObject *restrict holder,
			  Handle holder_slot, ReplyCap *restrict rc);
void ProcessUntrackReplyCap(ReplyCap *rc);

#endif // KERNEL_PROC_PROCESS_H

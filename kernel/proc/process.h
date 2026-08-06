#ifndef KERNEL_PROC_PROCESS_H
#define KERNEL_PROC_PROCESS_H

#include <stddef.h>
#include <stdint.h>
#include <list.h>
#include "kernel/ipc/port.h"
#include "kernel/ipc/handle.h"
#include "kernel/mm/vmm.h"
#include <arch/regs.h>
#include <zuzu/tls.h>
#include "thread.h"

#define MAX_PROCESSES 512

#define PROC_FLAG_INIT (1 << 0)   // PID 1
#define PROC_FLAG_DEVMGR (1 << 1) // hardware authority

extern void process_entry_trampoline(void);

typedef struct process
{
    Pid pid, parent_pid;
    addrspace_t *as;
    ListNode node; // embedded, not pointers
    ListNode destroy_node;
    ListNode timeout_node;
    Err exit_status;
    Pid waiting_for;
    char name[32];           // PROCESS name
    VirtAddr device_va_next; // initialized to USER_DEVICE_BASE in process_create
    VirtAddr mmap_va_next;   // initialized to USER_MMAP_BASE in process_create
    ListHead outstanding_replies;
    handle_vec_t handle_table;    
    uint32_t flags;
    Thread *thread;
    Tid waiting_for_tid;
    ListHead threads;
    ListHead children;
    ListNode sibling_node;
    PhysAddr tcb_page_pa;
    VirtAddr tcb_page_va;
    uint8_t tcb_slot_bitmap; /* bit N set = TCB slot N in use */
} ProcessObj;

_Static_assert(TCB_MAX_SLOTS <= 8, "tcb_slot_bitmap is 8 bits wide");

/* Returns the allocated slot index, or -1 if all slots are taken. */
static inline int TcbSlotAlloc(ProcessObj *p)
{
    for (uint32_t i = 0; i < TCB_MAX_SLOTS; i++) {
        if (!(p->tcb_slot_bitmap & (1u << i))) {
            p->tcb_slot_bitmap |= (uint8_t)(1u << i);
            return (int)i;
        }
    }
    return -1;
}

static inline void tcb_slot_free(ProcessObj *p, uint8_t slot)
{
    p->tcb_slot_bitmap &= (uint8_t)~(1u << slot);
}

void process_destroy(ProcessObj *process);
ProcessObj *process_find_by_pid(Pid pid);
ProcessObj *process_create(const char *name);
void process_wake_joiners(Tid tid, int32_t exit_status);
ProcessObj *process_load(const void *elf_data, size_t elf_size,
                                   const char *name, const char *argbuf,
                                   size_t argbuf_len, uint32_t argc);
void process_kill(ProcessObj *p, int exit_status);
void process_set_parent(ProcessObj *child, ProcessObj *parent);
ProcessObj *process_find_child_by_pid(ProcessObj *parent, Pid pid);
ProcessObj *process_find_zombie_child(ProcessObj *parent);
/* caller/holder/rc are always three distinct objects (never the same
 * process, never a process aliased with the reply-cap slab object). */
void process_track_reply_cap(ProcessObj *restrict caller, ProcessObj *restrict holder,
                             Handle holder_slot, ReplyCap *restrict rc);
void process_untrack_reply_cap(ReplyCap *rc);

#endif // KERNEL_PROC_PROCESS_H

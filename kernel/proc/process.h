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
    int32_t exit_status;
    Pid waiting_for;
    char name[32];           // PROCESS name
    VirtAddr device_va_next; // initialized to USER_DEVICE_BASE in process_create
    VirtAddr mmap_va_next;   // initialized to USER_MMAP_BASE in process_create
    list_head_t outstanding_replies;
    handle_vec_t handle_table;    
    uint32_t flags;
    thread_t *thread;
    Tid waiting_for_tid;
    list_head_t threads;
    list_head_t children;
    ListNode sibling_node;
    PhysAddr tcb_page_pa;
    VirtAddr tcb_page_va;
    uint8_t tcb_slot_bitmap; /* bit N set = TCB slot N in use */
} process_t;

_Static_assert(TCB_MAX_SLOTS <= 8, "tcb_slot_bitmap is 8 bits wide");

/* Returns the allocated slot index, or -1 if all slots are taken. */
static inline int tcb_slot_alloc(process_t *p)
{
    for (uint32_t i = 0; i < TCB_MAX_SLOTS; i++) {
        if (!(p->tcb_slot_bitmap & (1u << i))) {
            p->tcb_slot_bitmap |= (uint8_t)(1u << i);
            return (int)i;
        }
    }
    return -1;
}

static inline void tcb_slot_free(process_t *p, uint8_t slot)
{
    p->tcb_slot_bitmap &= (uint8_t)~(1u << slot);
}

void process_destroy(process_t *process);
process_t *process_find_by_pid(Pid pid);
process_t *process_create(const char *name);
void process_wake_joiners(Tid tid, int32_t exit_status);
process_t *process_load(const void *elf_data, size_t elf_size,
                                   const char *name, const char *argbuf,
                                   size_t argbuf_len, uint32_t argc);
void process_kill(process_t *p, int exit_status);
void process_set_parent(process_t *child, process_t *parent);
process_t *process_find_child_by_pid(process_t *parent, Pid pid);
process_t *process_find_zombie_child(process_t *parent);
void process_track_reply_cap(process_t *caller, process_t *holder,
                             Handle holder_slot, ReplyCap *rc);
void process_untrack_reply_cap(ReplyCap *rc);

#endif // KERNEL_PROC_PROCESS_H

#ifndef KERNEL_PROC_PROCESS_H
#define KERNEL_PROC_PROCESS_H

#include <stddef.h>
#include <stdint.h>
#include <list.h>
#include "kernel/ipc/endpoint.h"
#include "kernel/mm/vmm.h"
#include "arch/arm/include/context.h"
#include "thread.h"

#define MAX_PROCESSES 512

#define PROC_FLAG_INIT (1 << 0)   // PID 1, zombie reaper
#define PROC_FLAG_DEVMGR (1 << 1) // hardware authority

extern void process_entry_trampoline(void);

typedef struct process
{
    zpid_t pid, parent_pid;
    addrspace_t *as;
    list_node_t node; // embedded, not pointers
    list_node_t destroy_node;
    list_node_t timeout_node;
    int32_t exit_status;
    zpid_t waiting_for;
    char name[32];           // PROCESS name
    vaddr_t device_va_next; // initialized to USER_DEVICE_BASE in process_create
    vaddr_t mmap_va_next;   // initialized to USER_MMAP_BASE in process_create
    list_head_t outstanding_replies;
    handle_vec_t handle_table;    
    uint32_t flags;
    thread_t *thread;
    tid_t waiting_for_tid;
    list_head_t threads;
    list_head_t children;
    list_node_t sibling_node;
    paddr_t tcb_page_pa;
    vaddr_t tcb_page_va;
    uint32_t tcb_next_slot;
} process_t;

void process_destroy(process_t *process);
process_t *process_find_by_pid(zpid_t pid);
process_t *process_create(const char *name);
void process_wake_joiners(tid_t tid, int32_t exit_status);
process_t *process_load(const void *elf_data, size_t elf_size,
                                   const char *name, const char *argbuf,
                                   size_t argbuf_len, uint32_t argc);
void process_kill(process_t *p, int exit_status);
void process_set_parent(process_t *child, process_t *parent);
process_t *process_find_child_by_pid(process_t *parent, zpid_t pid);
process_t *process_find_zombie_child(process_t *parent);
void process_track_reply_cap(process_t *caller, process_t *holder,
                             handle_t holder_slot, reply_cap_t *rc);
void process_untrack_reply_cap(reply_cap_t *rc);

#endif // KERNEL_PROC_PROCESS_H

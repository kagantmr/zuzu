#ifndef _ZUZU_OBJECTS_SPACE_H
#define _ZUZU_OBJECTS_SPACE_H

#include "handle.h"
#include "kernel/mm/vmm.h"
#include "kernel/task/task.h"
#include <zuzu/tls.h>

typedef struct SpaceObjectStruct
{
    Pid pid, parent_pid;
    AddressSpace *as;
    ListNode node; // embedded, not pointers
    ListNode destroy_node;
    ListNode timeout_node;
    Pid waiting_for;
    char name[32]; // Space name
    ListHead outstanding_replies;
    HandleTable handle_table;
    TaskObject *thread;
    ListHead threads;
    ListHead children;
    ListNode sibling_node;
    PhysAddr tcb_page_pa[MAX_TCB_PAGES];         /* 37 entries */
    VirtAddr tcb_page_va;                        /* singular: contiguous window base */
    uint32_t tcb_slot_bitmap[BITMAP_WORDS(256)]; /* 256 bits */
} SpaceObject;

#endif /* _ZUZU_OBJECTS_SPACE_H */

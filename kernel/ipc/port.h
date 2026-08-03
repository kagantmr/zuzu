#ifndef ENDPOINT_H
#define ENDPOINT_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <list.h>
#include <vector.h>
#include "kernel/ipc/ntfn.h"
#include "kernel/mm/vmm.h"

struct process;

typedef struct endpoint {
    list_head_t sender_queue;
    list_head_t receiver_queue;
    Pid owner_pid;
    size_t ref_count;
    bool alive;
    ListNode node;
} Port;

typedef struct {
    Tid caller_tid;       // instead of zpid_t caller_pid
    Pid holder_pid;       // instead of process_t *holder
    Handle holder_slot;
    ListNode caller_link;
} ReplyCap;


#endif // ENDPOINT_H
#ifndef ENDPOINT_H
#define ENDPOINT_H

#include <stddef.h>
#include <stdbool.h>
#include <list.h>
#include <vector.h>
#include <zuzu/types.h>

struct process;

typedef struct endpoint {
    ListHead sender_queue;
    ListHead receiver_queue;
    Pid owner_pid;
    size_t ref_count;
    bool alive;
    ListNode node;
} Port;

typedef struct {
    Tid caller_tid;     
    Pid holder_pid;       
    Handle holder_slot;
    ListNode caller_link;
} ReplyCap;


#endif // ENDPOINT_H

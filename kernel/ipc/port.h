#ifndef PORT_H
#define PORT_H

#include <stddef.h>
#include <stdbool.h>
#include <list.h>
#include <vector.h>
#include <zuzu/types.h>

struct SpaceObjectStruct;

typedef struct {
    ListHead sender_queue;
    ListHead receiver_queue;
    Spid owner_pid;
    size_t ref_count;
    bool alive;
    ListNode node;
} PortObject;

typedef struct {
    Tid caller_tid;     
    Spid holder_pid;       
    Handle holder_slot;
    ListNode caller_link;
} ReplyCap;


#endif // PORT_H

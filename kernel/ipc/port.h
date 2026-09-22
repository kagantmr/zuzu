#ifndef _ZUZU_OBJECTS_PORT_H
#define _ZUZU_OBJECTS_PORT_H

#include <list.h>
#include <stdbool.h>
#include <stddef.h>
#include <vector.h>
#include <zuzu/types.h>

typedef struct SpaceObjectStruct SpaceObject;

/** */
typedef struct
{
    ListHead sender_queue;
    ListHead receiver_queue;
    SpaceObject *owner; // fast path
    Spid owner_spid;    // cross-check: owner->pid == owner_spid before trusting owner
    size_t ref_count;
    bool alive;
    ListNode node;
} PortObject;

typedef struct
{
    SpaceObject *caller; // fast path
    Tid caller_tid;      // for cross-check: caller->tid == caller_tid
    SpaceObject *holder; // fast path
    Spid holder_spid;    // for cross-check: holder->pid == holder_spid
    Handle holder_slot;
    ListNode caller_link;
} ReplyCap;

PortObject *PortCreate(SpaceObject *owner);
void PortDestroy(PortObject *port);

#endif /* _ZUZU_OBJECTS_PORT_H */

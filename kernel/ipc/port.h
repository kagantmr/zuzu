#ifndef _ZUZU_OBJECTS_PORT_H
#define _ZUZU_OBJECTS_PORT_H

#include <list.h>
#include <stdbool.h>
#include <stddef.h>
#include <types.h>
#include "event.h"
#include "observer.h"

typedef struct TaskObjectStruct TaskObject;
typedef struct SpaceObjectStruct SpaceObject;

/** */
typedef struct
{
    ListHead sender_queue;
    ListHead receiver_queue;
    Spid owner_spid;
    size_t ref_count;
    bool alive;
    ObserverSet observers;
} PortObject;

typedef struct
{
    TaskObject *caller_task; // fast path
    Tid caller_tid;      // for cross-check: caller->tid == caller_tid
} ReplyObject;

PortObject *PortCreate(SpaceObject *owner);
void PortUnref(PortObject *port);
void PortKill(PortObject *port);

/* A caller is queued with nobody receiving: the condition observers wait for. */
bool PortHasPending(const PortObject *port);

#endif /* _ZUZU_OBJECTS_PORT_H */

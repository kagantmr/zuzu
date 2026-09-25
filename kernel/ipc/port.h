#ifndef _ZUZU_OBJECTS_PORT_H
#define _ZUZU_OBJECTS_PORT_H

#include <list.h>
#include <stdbool.h>
#include <stddef.h>
#include <vector.h>
#include <zuzu/types.h>


typedef struct TaskObjectStruct TaskObject;
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
    TaskObject *caller_task; // fast path
    Tid caller_tid;      // for cross-check: caller->tid == caller_tid
    ListNode caller_link;
} EphemeralReplyObject;

PortObject *PortCreate(SpaceObject *owner);
void PortDestroy(PortObject *port);

/**
 * @brief Allocate space for a Port object.
 * @retval NULL Out of memory
 */
PortObject *PortObjAlloc(void);

/**
 * @brief Free space belonging to a Port object.
 */
void PortObjFree(PortObject *port);

#endif /* _ZUZU_OBJECTS_PORT_H */

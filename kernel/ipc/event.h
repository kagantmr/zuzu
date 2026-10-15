#ifndef KERNEL_IPC_EVENT_H
#define KERNEL_IPC_EVENT_H

#include <arch/regs.h>
#include <list.h>
#include <stdbool.h>
#include <types.h>

typedef struct SpaceObjectStruct SpaceObject;

#define EVENT_MEMMGMT_BIT (1u << 0)

typedef struct EventObjectStruct {
    EventWord word;
    ListHead wait_queue;
    Spid owner_spid;
    size_t ref_count;
    bool alive;
    EventWord bound_mask;
} EventObject;

void EventSignal(EventObject *ev, EventWord bits, bool bcast);

void EventWait(EventObject *ev, Duration timeout, CpuState *frame);

void EventUnref(EventObject *ev);
void EventKill(EventObject *ev);

EventObject *EventCreate(SpaceObject *owner);
void EventDestroy(EventObject *ev);

#endif // KERNEL_IPC_EVENT_H

#ifndef KERNEL_IPC_PORT_H
#define KERNEL_IPC_PORT_H

#include "event.h"
#include "observer.h"
#include <list.h>
#include <stdbool.h>
#include <stddef.h>
#include <types.h>

typedef struct TaskObjectStruct TaskObject;
typedef struct SpaceObjectStruct SpaceObject;

#define PORT_HELD_SLOTS 64

typedef struct {
    TaskObject *caller;
    Tid caller_tid;
    uint16_t gen;
} HeldCall;

typedef struct {
    ListHead sender_queue;
    ListHead receiver_queue;
    Spid owner_spid;
    size_t ref_count;
    bool alive;
    ObserverSet observers;
    ListHead active_servers;
    HeldCall *held;
} PortObject;

typedef struct {
    TaskObject *caller_task;
    Tid caller_tid;
} ReplyObject;

PortObject *PortCreate(SpaceObject *owner);
void PortUnref(PortObject *port);
void PortKill(PortObject *port);

bool PortHasPending(const PortObject *port);

#endif /* KERNEL_IPC_PORT_H */

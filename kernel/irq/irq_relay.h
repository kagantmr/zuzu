#ifndef _ZUZU_IRQ_RELAY_H
#define _ZUZU_IRQ_RELAY_H

#include "kernel/ipc/event.h"
#include "kernel/ipc/port.h"
#include "stdbool.h"
#include <arch/regs.h>

typedef struct SpaceObjectStruct SpaceObject;

typedef struct IrqOwnerStruct {
    SpaceObject *owner;
    bool pending;
    EventObject *bound_ev; // was Endpoint *bound_port
} IrqOwner;

void IrqReleaseAll(SpaceObject *owner);
bool IrqClearPending(Irq irq_num);
Err IrqBindToEvent(SpaceObject *owner, Irq irq_num, EventObject *ev);

#endif /* _ZUZU_IRQ_RELAY_H */

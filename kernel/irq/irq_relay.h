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
    uint32_t bit;          // EventWord bit this IRQ signals, 0..30 (31 is reserved)
} IrqOwner;

bool IrqIsValid(Irq irq_num);
void IrqReleaseAll(SpaceObject *owner);
bool IrqClearPending(Irq irq_num);
Err IrqBindToEvent(SpaceObject *owner, Irq irq_num, EventObject *ev, uint32_t bit);

/** Read-only view of the IRQ ownership table, indexed by IRQ line, for
 *  diagnostics (core/panic.c). */
const IrqOwner *GetIrqOwnersList(void);

#endif /* _ZUZU_IRQ_RELAY_H */

#ifndef KERNEL_IRQ_IRQ_RELAY_H
#define KERNEL_IRQ_IRQ_RELAY_H

#include "kernel/ipc/event.h"
#include "stdbool.h"
#include <arch/regs.h>

typedef struct SpaceObjectStruct SpaceObject;

typedef struct {
    SpaceObject *owner;
    bool pending;
    EventObject *bound_ev;
    uint32_t bit;          // EventWord bit this IRQ signals, 0..30 (31 is reserved)
} IrqOwner;

bool IrqIsValid(Irq irq_num);
void IrqReleaseAll(SpaceObject *owner);
Err IrqBindToEvent(SpaceObject *owner, Irq irq_num, EventObject *ev, uint32_t bit);
Err IrqRelayRearm(SpaceObject *owner, Irq irq_num);

/** Read-only view of the IRQ ownership table, indexed by IRQ line, for
 *  diagnostics (core/panic.c). */
const IrqOwner *GetIrqOwnersList(void);

#endif /* KERNEL_IRQ_IRQ_RELAY_H */

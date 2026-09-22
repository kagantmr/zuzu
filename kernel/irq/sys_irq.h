#ifndef SYS_IRQ_H
#define SYS_IRQ_H

#include "kernel/ipc/ntfn.h"
#include "kernel/ipc/port.h"
#include "stdbool.h"
#include <arch/regs.h>

typedef struct SpaceObjectStruct SpaceObject;

typedef struct irq_owner {
    SpaceObject *owner;
    bool pending;
    EventObject *bound_ntfn; // was Endpoint *bound_port
} IrqOwner;

void SysIrqBind(CpuState *frame);
void SysIrqDone(CpuState *frame);
void IrqReleaseAll(SpaceObject *owner);

bool IrqClearPending(int irq_num);

const IrqOwner *GetIrqOwners(void);

#endif // SYS_IRQ_H

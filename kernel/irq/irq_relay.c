/**
 * @file irq_relay.c
 * @brief Implementation of the interrupt relay.
 */

#include "irq_relay.h"
#include "kernel/sched/sched.h"
#include <arch/irq.h>
#include <compiler.h>

/** @brief Owner and binding of every interrupt line, indexed by line. */
static IrqOwner irq_owners[MAX_IRQS];

const IrqOwner *GetIrqOwnersList(void) { return irq_owners; }

/**
 * @brief Interrupt handler for every owned line: mask the line and signal the bound event.
 *
 * If no live event is bound the interrupt is remembered as pending and delivered on the next
 * bind. A dead event is dropped.
 *
 * @param[in] ctx The interrupt line, passed as a pointer.
 */
static void __hot RelayIrqHandler(void *ctx)
{
    Irq irq_num = (Irq)(VirtAddr)ctx;
    ArchIrqMaskLine(irq_num);

    irq_owners[irq_num].pending = true;
    EventObject *ev = irq_owners[irq_num].bound_ev;
    if (likely(ev && ev->alive)) {
        EventSignal(ev, (1U << irq_owners[irq_num].bit), false);
        irq_owners[irq_num].pending = false;
    } else if (ev) {
        irq_owners[irq_num].bound_ev = NULL;
        EventUnbindBit(ev, irq_owners[irq_num].bit);
        EventUnref(ev);
    }
}

bool IrqIsValid(Irq irq_num) { return (irq_num < MAX_IRQS) && !ArchIrqIsOwnedByKernel(irq_num); }

Err IrqBindToEvent(SpaceObject *owner, Irq irq_num, EventObject *ev, uint32_t bit)
{
    SpaceObject *current_owner = irq_owners[irq_num].owner;
    if (current_owner && current_owner != owner)
        return ERR_BUSY;

    if (!ev->alive)
        return ERR_DEAD;

    if (!current_owner) {
        irq_owners[irq_num] =
            (IrqOwner){.bound_ev = NULL, .owner = owner, .pending = false, .bit = bit};
        ArchIrqRegister(irq_num, RelayIrqHandler, (void *)(VirtAddr)irq_num);
    }
    if (irq_owners[irq_num].bound_ev) {
        EventObject *old = irq_owners[irq_num].bound_ev;
        EventUnbindBit(old, irq_owners[irq_num].bit);
        EventUnref(old);
    }

    irq_owners[irq_num].bit = bit;
    irq_owners[irq_num].bound_ev = ev;
    irq_owners[irq_num].bound_ev->ref_count++;
    EventBindBit(ev, bit);

    if (irq_owners[irq_num].pending) {
        EventSignal(irq_owners[irq_num].bound_ev, (1U << irq_owners[irq_num].bit), false);
        irq_owners[irq_num].pending = false;
    }

    ArchIrqUnmaskLine(irq_num);
    return ZUZU_OK;
}

void IrqRelease(SpaceObject *owner, Irq irq_num)
{
    if (irq_num >= MAX_IRQS || irq_owners[irq_num].owner != owner)
        return;
    EventObject *ev = irq_owners[irq_num].bound_ev;
    if (ev) {
        EventUnbindBit(ev, irq_owners[irq_num].bit);
        EventUnref(ev);
    }
    irq_owners[irq_num] = (IrqOwner){.bound_ev = NULL, .owner = NULL, .pending = false};
    ArchIrqMaskLine(irq_num);
}

void IrqReleaseAll(SpaceObject *owner)
{
    for (Irq irq_num = 0; irq_num < MAX_IRQS; irq_num++)
        IrqRelease(owner, irq_num);
}

Err IrqRelayRearm(SpaceObject *owner, Irq irq_num)
{
    if (irq_num >= MAX_IRQS)
        return ERR_BADARG;
    if (irq_owners[irq_num].owner != owner)
        return ERR_NOPERM;
    ArchIrqUnmaskLine(irq_num);
    return ZUZU_OK;
}

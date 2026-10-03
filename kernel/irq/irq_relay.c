#include "irq_relay.h"
#include "kernel/sched/sched.h"
#include <arch/irq.h>
#include <compiler.h>

static IrqOwner irq_owners[MAX_IRQS];

const IrqOwner *GetIrqOwnersList(void)
{
    return irq_owners;
}

static void __hot RelayIsr(void *ctx)
{
    Irq irq_num = (Irq)(VirtAddr)ctx;
    ArchIrqMaskLine(irq_num);

    irq_owners[irq_num].pending = true;
    EventObject *ev = irq_owners[irq_num].bound_ev;
    if (likely(ev && ev->alive))
    {
        EventSignal(ev, (1U << irq_owners[irq_num].bit), false);
        irq_owners[irq_num].pending = false;
    }
    else if (ev)
    {
        irq_owners[irq_num].bound_ev = NULL;
        EventDropReference(ev);
    }
}

bool IrqIsValid(Irq irq_num)
{
    return (irq_num < MAX_IRQS) && !ArchIrqIsOwnedByKernel(irq_num);
}

Err IrqBindToEvent(SpaceObject *owner, Irq irq_num, EventObject *ev, uint32_t bit)
{
    /* Ownership: free line is ours to claim; a line owned by someone else is busy. */
    SpaceObject *current_owner = irq_owners[irq_num].owner;
    if (current_owner && current_owner != owner)
        return ERR_BUSY;

    if (!ev->alive)
        return ERR_DEAD;

    /* Claim the line on first bind. */
    if (!current_owner)
    {
        irq_owners[irq_num] =
            (IrqOwner){.bound_ev = NULL, .owner = owner, .pending = false, .bit = bit};
        ArchIrqRegister(irq_num, RelayIsr, (void *)(VirtAddr)irq_num);
    }
    irq_owners[irq_num].bit = bit;

    if (irq_owners[irq_num].bound_ev)
    {
        EventObject *old = irq_owners[irq_num].bound_ev;
        EventDropReference(old);
    }

    irq_owners[irq_num].bound_ev = ev;
    irq_owners[irq_num].bound_ev->ref_count++;
    irq_owners[irq_num].bound_ev->bound_mask |= (1U << irq_owners[irq_num].bit);
    
    if (irq_owners[irq_num].pending)
    {
        EventSignal(irq_owners[irq_num].bound_ev, (1U << irq_owners[irq_num].bit), false);
        irq_owners[irq_num].pending = false;
    }

    ArchIrqUnmaskLine(irq_num);
    return ZUZU_OK;
}

void IrqReleaseAll(SpaceObject *owner)
{
    for (Irq irq_num = 0; irq_num < MAX_IRQS; irq_num++)
    {
        if (irq_owners[irq_num].owner == owner)
        {
            EventDropReference(irq_owners[irq_num].bound_ev);
            irq_owners[irq_num] = (IrqOwner){.bound_ev = NULL, .owner = NULL, .pending = false};
            ArchIrqMaskLine(irq_num);
        }
    }
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
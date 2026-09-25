#include "irq_relay.h"
#include "kernel/bench.h"
#include "kernel/mm/alloc.h"
#include "kernel/sched/sched.h"
#include "kernel/svc/svc.h"
#include <arch/barrier.h>
#include <arch/irq.h>
#include <compiler.h>
#include <string.h>

static IrqOwner irq_owners[MAX_IRQS];

#define LOG_FMT(fmt) "(syscall_irq) " fmt
#include "core/log.h"

const IrqOwner *GetIrqOwnersList(void)
{
    return irq_owners;
}

static void __hot RelayIsr(void *ctx)
{
    Irq irq_num = (Irq)(VirtAddr)ctx;
    ArchIrqMaskLine(irq_num);

    irq_owners[irq_num].pending = true;
    EventObject *ntfn = irq_owners[irq_num].bound_ev;
    if (likely(ntfn && ntfn->alive))
    {
        EventSignal(ntfn, (1U << (irq_num & 31)));
        irq_owners[irq_num].pending = false;
    }
    else if (ntfn && !ntfn->alive)
    {
        irq_owners[irq_num].bound_ev = NULL;
    }
}

bool IrqIsValid(Irq irq_num)
{
    return (irq_num < MAX_IRQS) && !ArchIrqIsOwnedByKernel(irq_num);
}

Err IrqBindToEvent(SpaceObject *owner, Irq irq_num, EventObject *ev)
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
            (IrqOwner){.bound_ev = NULL, .owner = owner, .pending = false};
        ArchIrqRegister(irq_num, RelayIsr, (void *)(VirtAddr)irq_num);
    }

    if (irq_owners[irq_num].bound_ev)
    {
        EventObject *old = irq_owners[irq_num].bound_ev;
        old->irq_bind_count--;
        EventDropReference(old);
    }

    irq_owners[irq_num].bound_ev = ev;
    irq_owners[irq_num].bound_ev->ref_count++;
    irq_owners[irq_num].bound_ev->irq_bind_count++;

    if (irq_owners[irq_num].pending)
    {
        EventSignal(irq_owners[irq_num].bound_ev, (1U << (irq_num & 31)));
        irq_owners[irq_num].pending = false;
    }

    ArchIrqUnmaskLine(irq_num);
    return ZUZU_OK;
}

bool IrqClearPending(Irq irq_num)
{
    if (irq_num >= MAX_IRQS)
        return false;
    if (irq_owners[irq_num].pending)
    {
        irq_owners[irq_num].pending = false;
        return true;
    }
    return false;
}

void IrqReleaseAll(SpaceObject *owner)
{
    for (Irq irq_num = 0; irq_num < MAX_IRQS; irq_num++)
    {
        if (irq_owners[irq_num].owner == owner)
        {
            irq_owners[irq_num].bound_ev->irq_bind_count--;
            EventDropReference(irq_owners[irq_num].bound_ev);
            irq_owners[irq_num] = (IrqOwner){.bound_ev = NULL, .owner = NULL, .pending = false};
            ArchIrqMaskLine(irq_num);
        }
    }
}

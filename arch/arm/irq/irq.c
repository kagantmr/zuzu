// irq.c - ARM IRQ handling implementation

#include "arch/arm/include/gicv2.h"
#include "arch/arm/timer/generic_timer.h"
#include <arch/irq.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LOG_FMT(fmt) "(irq) " fmt
#include "core/log.h"

IrqHandler handler_table[MAX_IRQS];
void *handler_ctx[MAX_IRQS];

bool ArchIrqIsOwnedByKernel(uint32_t irq_id)
{
    switch (irq_id) {
    case TIMER_IRQ_VIRT: // ARM generic timer CNTV PPI
        return true;
    default:
        return false;
    }
}

void ArchIrqInit(void)
{
    // Clear handler table
    for (uint32_t i = 0; i < MAX_IRQS; i++) {
        handler_table[i] = NULL;
        handler_ctx[i] = NULL;
    }
}

bool ArchIrqRegister(uint32_t irq_id, IrqHandler handler, void *ctx)
{
    if (irq_id >= MAX_IRQS || handler == NULL) {
        return false;
    }
    handler_table[irq_id] = handler;
    handler_ctx[irq_id] = ctx;
    GicV2ConfigureIrq(irq_id);
    return true;
}

bool ArchIrqHasHandler(uint32_t irq_id)
{
    return irq_id < MAX_IRQS && handler_table[irq_id] != NULL;
}

void *ArchIrqHandlerAddr(uint32_t irq_id)
{
    return irq_id < MAX_IRQS ? (void *)handler_table[irq_id] : NULL;
}

void ArchIrqSetPrio(Irq irq_id, uint8_t prio) { GicV2SetPriority(irq_id, prio); }

void ArchIrqMaskLine(uint32_t irq_id)
{
    GicV2MaskIrq(irq_id); // Delegate to GIC function
}
void ArchIrqUnmaskLine(uint32_t irq_id)
{
    GicV2UnmaskIrq(irq_id); // Delegate to GIC function
}

void ArchIrqDispatch(void)
{
    uint32_t iar = GicAcknowledge();
    uint32_t irq_id = iar & 0x3FF;

    if (irq_id == 1023) {
        return; // Spurious interrupt, ignore
    }
    if (irq_id < MAX_IRQS && handler_table[irq_id] != NULL) {
        handler_table[irq_id](handler_ctx[irq_id]);
    } else {
        KERROR("Unhandled IRQ %u", irq_id);
    }
    GicEnd(iar);
}
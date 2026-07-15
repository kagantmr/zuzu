// irq.c - ARM IRQ handling implementation

#include <arch/irq.h>
#include "arch/arm/include/gicv2.h"
#include "arch/arm/timer/generic_timer.h"

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define LOG_FMT(fmt) "(irq) " fmt
#include "core/log.h"

irq_handler_t handler_table[MAX_IRQS];
void* handler_ctx[MAX_IRQS];

bool arch_irq_is_reserved(uint32_t irq_id) {
    switch (irq_id) {
    case TIMER_IRQ_VIRT:   // ARM generic timer CNTV PPI
        return true;
    default:
        return false;
    }
}

void arch_irq_init(void) {
    // Clear handler table
    for (uint32_t i = 0; i < MAX_IRQS; i++)
    {
        handler_table[i] = NULL;
        handler_ctx[i] = NULL;
    }
}

bool arch_irq_register(uint32_t irq_id, irq_handler_t handler, void *ctx) {
    if (irq_id >= MAX_IRQS || handler == NULL) {
        return false;
    }
    handler_table[irq_id] = handler;
    handler_ctx[irq_id] = ctx;
    return true;
}

bool arch_irq_unregister(uint32_t irq_id) {
    if (irq_id >= MAX_IRQS) {
        return false;
    }
    handler_table[irq_id] = NULL;
    handler_ctx[irq_id] = NULL;
    return true;    
}

void arch_irq_disable_line(uint32_t irq_id) {
    gic_disable_irq(irq_id); // Delegate to GIC function
}
void arch_irq_enable_line(uint32_t irq_id) {
    gic_enable_irq(irq_id); // Delegate to GIC function
}

void arch_irq_dispatch(void) {
    //KINFO("IRQ received");
    uint32_t iar = gic_acknowledge();
    uint32_t irq_id = iar & 0x3FF;

    if (irq_id == 1023) {
        return;  // Spurious interrupt, ignore
    }
    if (irq_id < MAX_IRQS && handler_table[irq_id] != NULL) {
        handler_table[irq_id](handler_ctx[irq_id]);
    } else {
        KERROR("Unhandled IRQ %u", irq_id);
    }
    gic_end(iar);
    

}
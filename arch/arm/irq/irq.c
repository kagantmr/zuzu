#include "arch/arm/include/irq.h"
#include "arch/arm/include/gicv2.h"
#include "core/log.h"

#include <stddef.h>
#include <stdint.h>

irq_handler_t handler_table[MAX_IRQS];
void* handler_ctx[MAX_IRQS];

void arch_global_irq_disable() {
    __asm__ volatile (
        "cpsid i\n"
    );
};

void arch_global_irq_enable() {
    __asm__ volatile (
        "cpsie i\n"
    );
}

void irq_init(void) {
    // Clear handler table
    for (uint32_t i = 0; i < MAX_IRQS; i++)
    {
        handler_table[i] = NULL;
        handler_ctx[i] = NULL;
    }
}

bool irq_register(uint32_t irq_id, irq_handler_t handler, void *ctx) {
    if (irq_id >= MAX_IRQS || handler == NULL) {
        return false;
    }
    handler_table[irq_id] = handler;
    handler_ctx[irq_id] = ctx;
    return true;
}

bool irq_unregister(uint32_t irq_id) {
    if (irq_id >= MAX_IRQS) {
        return false;
    }
    handler_table[irq_id] = NULL;
    handler_ctx[irq_id] = NULL;
    return true;    
}

void irq_disable_line(uint32_t irq_id) {
    gic_disable_irq(irq_id); // Delegate to GIC function
}
void irq_enable_line(uint32_t irq_id) {
    gic_enable_irq(irq_id); // Delegate to GIC function
}

void irq_dispatch(void) {
    //KINFO("IRQ received");
    uint32_t iar = gic_acknowledge();
    uint32_t irq_id = iar & 0x3FF;

    if (irq_id == 1023) {
        return;  // Spurious interrupt, ignore
    }

    gic_end(iar);

    if (irq_id < MAX_IRQS && handler_table[irq_id] != NULL) {
        handler_table[irq_id](handler_ctx[irq_id]);
    } else {
        KERROR("Unhandled IRQ %u", irq_id);
    }
    
}
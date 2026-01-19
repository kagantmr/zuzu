#include "arch/arm/include/irq.h"


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

void arch_global_irq_disable();


void arch_global_irq_enable();

void irq_init(void);

bool irq_register(uint32_t irq_id, irq_handler_t handler, void *ctx);
bool irq_unregister(uint32_t irq_id);

void irq_disable_line(uint32_t irq_id);
void irq_enable_line(uint32_t irq_id);

void irq_dispatch(void);
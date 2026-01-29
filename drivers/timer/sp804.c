#include "sp804.h"
#include "arch/arm/include/irq.h"
#include <lib/io.h>
#include "core/log.h"


static void sp804_irq_handler(void* ctx) {

    (void)ctx;

    sp804_clear_interrupt((uintptr_t)ctx);
}

void sp804_init(uintptr_t base_addr, uint32_t load_value) {
    sp804_set_load_value(base_addr, load_value);
    sp804_clear_interrupt(base_addr);
    sp804_stop(base_addr);
    irq_register(SP804_IRQ, sp804_irq_handler, (void*)base_addr);
    irq_enable_line(SP804_IRQ);
}

void sp804_start(uintptr_t base_addr) {
    uint32_t control = SP804_TIMER_CONTROL_ENABLE |
                       SP804_TIMER_CONTROL_PERIODIC |
                       SP804_TIMER_CONTROL_INT_ENABLE |
                       SP804_TIMER_CONTROL_32BIT |
                       SP804_TIMER_CONTROL_PRESCALE_1;
    writel(base_addr + SP804_TIMER0_OFFSET + SP804_TIMER_CONTROL, control);
}

void sp804_stop(uintptr_t base_addr) {
    uint32_t control = readl(base_addr + SP804_TIMER0_OFFSET + SP804_TIMER_CONTROL);
    control &= ~SP804_TIMER_CONTROL_ENABLE;
    writel(base_addr + SP804_TIMER0_OFFSET + SP804_TIMER_CONTROL, control);
}

void sp804_clear_interrupt(uintptr_t base_addr) {
    writel(base_addr + SP804_TIMER0_OFFSET + SP804_TIMER_INTCLR, 1);
}

void sp804_set_load_value(uintptr_t base_addr, uint32_t load_value) {
    writel(base_addr + SP804_TIMER0_OFFSET + SP804_TIMER_LOAD, load_value);
}



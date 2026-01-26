#include "sp804.h"
#include "lib/io.h"

void sp804_init(uintptr_t base_addr, uint32_t load_value) {
    sp804_set_load_value(base_addr, load_value);
    sp804_clear_interrupt(base_addr);
    sp804_stop(base_addr);
}

void sp804_start(uintptr_t base_addr) {
    uint32_t control = SP804_TIMER_CONTROL_ENABLE |
                       SP804_TIMER_CONTROL_PERIODIC |
                       SP804_TIMER_CONTROL_INT_ENABLE |
                       SP804_TIMER_CONTROL_32BIT |
                       SP804_TIMER_CONTROL_PRESCALE_1;
    writel(base_addr + SP804_TIMER_CONTROL, control);
}

void sp804_stop(uintptr_t base_addr) {
    uint32_t control = readl(base_addr + SP804_TIMER_CONTROL);
    control &= ~SP804_TIMER_CONTROL_ENABLE;
    writel(base_addr + SP804_TIMER_CONTROL, control);
}

void sp804_clear_interrupt(uintptr_t base_addr) {
    writel(base_addr + SP804_TIMER_INTCLR, 1);
}

void sp804_set_load_value(uintptr_t base_addr, uint32_t load_value) {
    writel(base_addr + SP804_TIMER_LOAD, load_value);
}


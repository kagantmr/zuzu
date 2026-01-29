#ifndef SP804_H
#define SP804_H

#include <stdint.h>
#include <stddef.h>

#define SP804_IRQ  34  // SPI 2 + 32

#define SP804_TIMER0_OFFSET 0x00
#define SP804_TIMER1_OFFSET 0x20
#define SP804_TIMER_LOAD 0x00
#define SP804_TIMER_VALUE 0x04
#define SP804_TIMER_CONTROL 0x08
#define SP804_TIMER_INTCLR 0x0C
#define SP804_TIMER_RIS 0x10
#define SP804_TIMER_MIS 0x14
#define SP804_TIMER_BGLOAD 0x18

#define SP804_TIMER_CONTROL_ENABLE (1 << 7)
#define SP804_TIMER_CONTROL_PERIODIC (1 << 6)
#define SP804_TIMER_CONTROL_INT_ENABLE (1 << 5)
#define SP804_TIMER_CONTROL_32BIT (1 << 1)
#define SP804_TIMER_CONTROL_PRESCALE_1 (0 << 2)
#define SP804_TIMER_CONTROL_PRESCALE_16 (1 << 2)
#define SP804_TIMER_CONTROL_PRESCALE_256 (2 << 2)
#define SP804_TIMER_CONTROL_ONESHOT (1 << 0)
#define SP804_TIMER_CONTROL_16BIT  (0 << 1)
#define SP804_TIMER_CONTROL_SIZE_MASK (1 << 1)
#define SP804_TIMER_CONTROL_PRESCALE_MASK (3 << 2)
#define SP804_TIMER_CONTROL_MODE_MASK (1 << 6)
#define SP804_TIMER_CONTROL_INT_MASK (1 << 5)

void sp804_init(uintptr_t base_addr, uint32_t load_value);
void sp804_start(uintptr_t base_addr);
void sp804_stop(uintptr_t base_addr);
void sp804_clear_interrupt(uintptr_t base_addr);
void sp804_set_load_value(uintptr_t base_addr, uint32_t load_value);

#endif // SP804_H



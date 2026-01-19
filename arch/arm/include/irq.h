#ifndef IRQ_H
#define IRQ_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Disable all interrupts (ARM).
 */ 
void arch_global_irq_disable();

/**
 * @brief Enable all interrupts (ARM).
 */
void arch_global_irq_enable();

void irq_init(void);


typedef void (*irq_handler_t)(void *ctx);
bool irq_register(uint32_t irq_id, irq_handler_t handler, void *ctx);
bool irq_unregister(uint32_t irq_id);

void irq_disable_line(uint32_t irq_id);
void irq_enable_line(uint32_t irq_id);

void irq_dispatch(void);

#endif
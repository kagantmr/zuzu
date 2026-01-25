#ifndef IRQ_H
#define IRQ_H

#include <stdint.h>
#include <stdbool.h>

typedef void (*irq_handler_t)(void *ctx);

#define MAX_IRQS 256

/**
 * @brief Disable all interrupts (ARM).
 */ 
void arch_global_irq_disable();

/**
 * @brief Enable all interrupts (ARM).
 */
void arch_global_irq_enable();

/**
 * @brief Initialize the IRQ subsystem and the GIC.
 */
void irq_init(void);

/**
 * @brief Register an IRQ handler for a specific IRQ ID.
 * @param irq_id The IRQ number to register the handler for.
 * @param handler The function to call when the IRQ occurs.
 * @param ctx A context pointer passed to the handler when invoked.
 * @return true on success, false on failure.
 */
bool irq_register(uint32_t irq_id, irq_handler_t handler, void *ctx);

/**
 * @brief Unregister the IRQ handler for a specific IRQ ID.
 * @param irq_id The IRQ number to unregister the handler for.
 * @return true on success, false on failure.
 */
bool irq_unregister(uint32_t irq_id);

/**
 * @brief Disable a specific IRQ line.
 * @param irq_id The IRQ number to disable.
 */
void irq_disable_line(uint32_t irq_id);

/**
 * @brief Enable a specific IRQ line.
 * @param irq_id The IRQ number to enable.
 */
void irq_enable_line(uint32_t irq_id);

/**
 * @brief Dispatch the IRQ to the appropriate handler.
 */
void irq_dispatch(void);

#endif
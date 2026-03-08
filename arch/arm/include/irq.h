#ifndef IRQ_H
#define IRQ_H

#include <stdint.h>
#include <stdbool.h>

typedef void (*irq_handler_t)(void *ctx); /* Generic IRQ handler function ptr */

#define MAX_IRQS 128

static inline void arch_global_irq_disable(void) {
    __asm__ volatile("cpsid i" ::: "memory");
}

static inline void arch_global_irq_enable(void) {
    __asm__ volatile("cpsie i" ::: "memory");
}

static inline uint32_t arch_irq_save(void) {
    uint32_t cpsr;
    __asm__ volatile("mrs %0, cpsr" : "=r"(cpsr) :: "memory");
    __asm__ volatile("cpsid i" ::: "memory");
    return cpsr;
}

static inline void arch_irq_restore(uint32_t state) {
    __asm__ volatile("msr cpsr_c, %0" :: "r"(state) : "memory");
}

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
// arch/irq.h - Neutral interrupt-controller contract.
//
// Per-line interrupt management and dispatch, backed by the architecture's
// interrupt controller (GICv2 on ARM). Global IRQ-flag control lives in
// <arch/cpu.h>; this header is about individual IRQ lines and handlers.

#ifndef ZUZU_ARCH_IRQ_H
#define ZUZU_ARCH_IRQ_H

#include <stdint.h>
#include <stdbool.h>

typedef void (*irq_handler_t)(void *ctx); /* generic IRQ handler */

#define MAX_IRQS 128

/** Initialize the interrupt subsystem (handler table + controller). */
void arch_irq_init(void);

/** Register a handler for an IRQ line. Returns true on success. */
bool arch_irq_register(uint32_t irq_id, irq_handler_t handler, void *ctx);

/** Unregister the handler for an IRQ line. Returns true on success. */
bool arch_irq_unregister(uint32_t irq_id);

/** Disable a single IRQ line at the controller. */
void arch_irq_disable_line(uint32_t irq_id);

/** Enable a single IRQ line at the controller. */
void arch_irq_enable_line(uint32_t irq_id);

/** Dispatch the currently-pending IRQ to its registered handler. */
void arch_irq_dispatch(void);

/** True if an IRQ line is reserved by the kernel/arch (e.g. the tick timer)
 *  and therefore cannot be claimed by a userspace driver. */
bool arch_irq_is_reserved(uint32_t irq_id);

/* ---- Controller introspection (for diagnostics / panic dumps) ----------- */
/* Lines are reported 32 per "word"; there are MAX_IRQS/32 words. */

/** True once the interrupt controller has been initialized. */
bool arch_irq_ready(void);

/** Current priority-mask threshold (controller-defined units). */
uint32_t arch_irq_priority_mask(void);

/** Bitmap word of enabled IRQ lines [word*32, word*32+32). */
uint32_t arch_irq_enabled_word(uint32_t word);

/** Bitmap word of pending IRQ lines [word*32, word*32+32). */
uint32_t arch_irq_pending_word(uint32_t word);

#endif // ZUZU_ARCH_IRQ_H

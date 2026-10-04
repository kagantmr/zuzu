// arch/irq.h - Neutral interrupt-controller contract.
//
// Per-line interrupt management and dispatch, backed by the architecture's
// interrupt controller (GICv2 on ARM). Global IRQ-flag control lives in
// <arch/cpu.h>; this header is about individual IRQ lines and handlers.

#ifndef ARCH_IRQ_H
#define ARCH_IRQ_H

#include <stdint.h>
#include <stdbool.h>

typedef void (*IrqHandler)(void *ctx); /* generic IRQ handler */

// Must cover the GIC's configured SPI range (GicInit() sets up 256 lines);
// real hardware (e.g. rpi4/BCM2711) uses SPI numbers well past 128.
#define MAX_IRQS 256

/** Initialize the interrupt subsystem (handler table + controller). */
void ArchIrqInit(void);

/** Register a handler for an IRQ line. Returns true on success. */
bool ArchIrqRegister(uint32_t irq_id, IrqHandler handler, void *ctx);

/** Unregister the handler for an IRQ line. Returns true on success. */

/** Disable a single IRQ line at the controller. */
void ArchIrqMaskLine(uint32_t irq_id);

/** Enable a single IRQ line at the controller. */
void ArchIrqUnmaskLine(uint32_t irq_id);

/** Set an IRQ line's priority (controller-defined units; lower preempts higher). */
void ArchIrqSetPrio(uint32_t irq_id, uint8_t prio);

/** Dispatch the currently-pending IRQ to its registered handler. */
void ArchIrqDispatch(void);

/** True if an IRQ line is reserved by the kernel/arch (e.g. the tick timer)
 *  and therefore cannot be claimed by a userspace driver. */
bool ArchIrqIsOwnedByKernel(uint32_t irq_id);

/* ---- Controller introspection (for diagnostics / panic dumps) ----------- */
/* Lines are reported 32 per "word"; there are MAX_IRQS/32 words. */

/** True once the interrupt controller has been initialized. */
bool ArchIrqReady(void);

/** Current priority-mask threshold (controller-defined units). */
uint32_t ArchIrqPriorityMask(void);

/** Bitmap word of enabled IRQ lines [word*32, word*32+32). */
uint32_t ArchIrqEnabledWord(uint32_t word);

/** Bitmap word of pending IRQ lines [word*32, word*32+32). */
uint32_t ArchIrqPendingWord(uint32_t word);

/** True if a kernel-level handler is registered for this IRQ line. */
bool ArchIrqHasHandler(uint32_t irq_id);

/** Address of the registered handler, for symbolization in diagnostics;
 *  NULL if none. */
void *ArchIrqHandlerAddr(uint32_t irq_id);

#endif // ARCH_IRQ_H

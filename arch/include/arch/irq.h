#ifndef ARCH_IRQ_H
#define ARCH_IRQ_H

#include <stdbool.h>
#include <stdint.h>

typedef void (*IrqHandler)(void *ctx);

#define MAX_IRQS 256

void ArchIrqInit(void);

bool ArchIrqRegister(uint32_t irq_id, IrqHandler handler, void *ctx);

void ArchIrqMaskLine(uint32_t irq_id);

void ArchIrqUnmaskLine(uint32_t irq_id);

void ArchIrqSetPrio(uint32_t irq_id, uint8_t prio);

void ArchIrqDispatch(void);

bool ArchIrqIsOwnedByKernel(uint32_t irq_id);

bool ArchIrqReady(void);

uint32_t ArchIrqFirstDeviceLine(void);

uint32_t ArchIrqPriorityMask(void);

bool ArchIrqIsEnabled(uint32_t line);

bool ArchIrqIsPending(uint32_t line);

bool ArchIrqHasHandler(uint32_t irq_id);

void *ArchIrqHandlerAddr(uint32_t irq_id);

#endif // ARCH_IRQ_H

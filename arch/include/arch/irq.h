/**
 * @file irq.h
 * @brief Architecture neutral interrupt handling interface.
 */

#ifndef ARCH_IRQ_H
#define ARCH_IRQ_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Interrupt handler function pointer type.
 */
typedef void (*IrqHandler)(void *ctx);

/**
 * @brief The maximum number of interrupts allowed in the interface.
 */
#define MAX_IRQS 256

/**
 * @brief Initialize the interrupt subsystem.
 */
void ArchIrqInit(void);

/**
 * @brief Register a handler for an interrupt.
 * 
 * @param[in] irq_id Interrupt number.
 * @param[in] handler The handler to register.
 * @param[in] ctx  The arguments to pass to the handler.
 * 
 * @return bool true if registration is complete.
 */
bool ArchIrqRegister(uint32_t irq_id, IrqHandler handler, void *ctx);

/**
 * @brief Mask an interrupt, ignoring it if it fires or not.
 * 
 * @param[in] irq_id The interrupt number.
 */
void ArchIrqMaskLine(uint32_t irq_id);

/**
 * @brief Unmask an interrupt and allow it to interrupt execution.
 * 
 * @param[in] irq_id The interrupt number.
 */
void ArchIrqUnmaskLine(uint32_t irq_id);

/**
 * @brief Change the priority of an interrupt.
 * 
 * @param[in] irq_id The interrupt number.
 * @param[in] prio The new priority
 */
void ArchIrqSetPrio(uint32_t irq_id, uint8_t prio);

/**
 * @brief Interrupt dispatch (inside the exception handler).
 */
void ArchIrqDispatch(void);

/**
 * @brief returns true if the interrupt with the given number is kernel-owned.
 * 
 * @param[in] irq_id The interrupt number.
 * 
 * @return bool true if the interrupt is kernel owned.
 */
bool ArchIrqIsOwnedByKernel(uint32_t irq_id);

/**
 * @brief Check if the interrupt controller is initialized.
 * 
 * @return bool true if the interrupt controller is ready.
 */
bool ArchIrqReady(void);

/**
 * @brief Getter for the first index peripheral interrupts start.
 * 
 * @return uint32_t The start value.
 */
uint32_t ArchIrqFirstDeviceLine(void);

/**
 * @brief Getter for the interrupt priority mask
 * 
 * @return uint32_t The mask value.
 */
uint32_t ArchIrqPriorityMask(void);

/**
 * @brief Check if an interrupt is enabled. 
 * 
 * @param[in] line The interrupt number.
 * 
 * @return bool true if the interrupt is enabled.
 */
bool ArchIrqIsEnabled(uint32_t line);

/**
 * @brief Check if an interrupt is pending. 
 * 
 * @param[in] line The interrupt number.
 * 
 * @return bool true if the interrupt is pending.
 */
bool ArchIrqIsPending(uint32_t line);

/**
 * @brief Check if the interrupt has a handler
 * 
 * @param[in] irq_id The interrupt number.
 * 
 * @return bool true if the interrupt is assigned a handler.
 */
bool ArchIrqHasHandler(uint32_t irq_id);

/**
 * @brief Get the handler routine for the given interrupt number.
 * 
 * @param[in] irq_id The interrupt number.
 * 
 * @return void* Function pointer of the handler.
 */
void *ArchIrqHandlerAddr(uint32_t irq_id);

#endif // ARCH_IRQ_H

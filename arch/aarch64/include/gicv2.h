/**
 * @file
 * @brief GICv2 interrupt controller registers and driver.
 */

#ifndef ARCH_ARM_GICV2_H
#define ARCH_ARM_GICV2_H

#include <stdint.h>
#include <types.h>

/** @brief Distributor control register. */
#define GICD_CTLR 0x000
/** @brief Distributor interrupt group registers. */
#define GICD_IGROUPR 0x080
/** @brief Distributor set-enable registers. */
#define GICD_ISENABLER 0x100
/** @brief Distributor clear-enable registers. */
#define GICD_ICENABLER 0x180
/** @brief Distributor set-pending registers. */
#define GICD_ISPENDER 0x200
/** @brief Distributor priority registers, one byte per interrupt. */
#define GICD_IPRIORITYR 0x400
/** @brief Distributor CPU target registers, one byte per interrupt. */
#define GICD_ITARGETSR 0x800
/** @brief Distributor configuration registers: level or edge, two bits per interrupt. */
#define GICD_ICFGR 0xC00

/** @brief CPU interface control register. */
#define GICC_CTLR 0x000
/** @brief CPU interface priority mask register. */
#define GICC_PMR 0x004
/** @brief CPU interface acknowledge register. */
#define GICC_IAR 0x00C
/** @brief CPU interface end-of-interrupt register. */
#define GICC_EOIR 0x010

/**
 * @brief Brings up the GIC: group 0, priority 0xA0 and CPU 0 as target for every
 * interrupt, then enables the distributor and the CPU interface with no priority masking.
 *
 * @param[in] gicd_base_addr Mapped distributor base.
 * @param[in] gicc_base_addr Mapped CPU interface base.
 */
void GicInit(uintptr_t gicd_base_addr, uintptr_t gicc_base_addr);

/**
 * @brief Makes a shared peripheral interrupt level-triggered and routes it to CPU 0.
 *
 * @param[in] irq_id Interrupt ID; IDs below 32 are left alone.
 */
void GicV2ConfigureIrq(Irq irq_id);

/**
 * @brief Enables an interrupt in the distributor.
 *
 * @param[in] irq_id Interrupt ID.
 */
void GicV2UnmaskIrq(Irq irq_id);

/**
 * @brief Disables an interrupt in the distributor.
 *
 * @param[in] irq_id Interrupt ID.
 */
void GicV2MaskIrq(Irq irq_id);

/**
 * @brief Sets an interrupt's priority.
 *
 * @param[in] irq_id   Interrupt ID.
 * @param[in] priority Priority; lower values are more urgent.
 */
void GicV2SetPriority(Irq irq_id, uint8_t priority);

/**
 * @brief Acknowledges the highest-priority pending interrupt.
 *
 * @return The IAR value; bits [9:0] are the interrupt ID, 1023 means spurious. Pass it to GicEnd().
 */
uint32_t GicAcknowledge(void);

/**
 * @brief Signals the end of an interrupt.
 *
 * @param[in] iar Value returned by GicAcknowledge().
 */
void GicEnd(uint32_t iar);

#endif

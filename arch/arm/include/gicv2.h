#ifndef GICV2_H
#define GICV2_H

#include <stdint.h>

#define GIC_SPI_BASE  32   /* SPIs start at 32 */
#define GIC_PPI_BASE  16   /* PPIs start at 16 */

/* Distributor */
#define GICD_CTLR        0x000
#define GICD_IGROUPR     0x080 
#define GICD_ISENABLER   0x100
#define GICD_ICENABLER   0x180
#define GICD_IPRIORITYR  0x400
#define GICD_ITARGETSR  0x800
#define GICD_ICFGR      0xC00

/* CPU Interface */
#define GICC_CTLR       0x000
#define GICC_PMR        0x004
#define GICC_IAR        0x00C
#define GICC_EOIR       0x010

/**
 * @brief Initialize the GICv2 distributor and CPU interface.
 */
void gic_init(uintptr_t gicd_base_addr, uintptr_t gicc_base_addr);

/**
 * @brief Enable a specific IRQ in the GIC.
 * @param irq_id The IRQ number to enable.
 */
void gic_enable_irq(uint32_t irq_id);

/**
 * @brief Disable a specific IRQ in the GIC.
 * @param irq_id The IRQ number to disable.
 */
void gic_disable_irq(uint32_t irq_id);

/**
 * @brief Acknowledge an IRQ and return the raw IAR value.
 *
 * This function returns the raw Interrupt Acknowledge Register (IAR)
 * value, which contains the INTID in the low 10 bits.
 *
 * @return The raw IAR value.
 */
uint32_t gic_acknowledge(void);

/**
 * @brief Signal the end of an IRQ to the GIC.
 *
 * Callers must pass the raw IAR value returned by gic_acknowledge().
 *
 * @param iar The raw IAR value to signal completion for.
 */
void gic_end(uint32_t iar);


#endif

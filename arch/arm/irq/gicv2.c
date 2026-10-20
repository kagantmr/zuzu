/**
 * @file
 * @brief GICv2 driver.
 */

#include "arch/arm/include/gicv2.h"
#include <arch/barrier.h>
#include <arch/irq.h>
#include <stdbool.h>
#include <types.h>

volatile uint32_t *gicd_base; /**< Mapped GIC distributor; NULL until GicInit(). */
volatile uint32_t *gicc_base; /**< Mapped GIC CPU interface; NULL until GicInit(). */

/**
 * @brief Writes a distributor register.
 *
 * @param[in] off Register offset.
 * @param[in] val Value.
 */
static inline void GicdWrite(uint32_t off, uint32_t val) { gicd_base[off >> 2] = val; }

/**
 * @brief Reads a distributor register.
 *
 * @param[in] off Register offset.
 * @return The register value.
 */
static inline uint32_t GicdRead(uint32_t off) { return gicd_base[off >> 2]; }

/**
 * @brief Writes a CPU interface register.
 *
 * @param[in] off Register offset.
 * @param[in] val Value.
 */
static inline void GiccWrite(uint32_t off, uint32_t val) { gicc_base[off >> 2] = val; }

/**
 * @brief Reads a CPU interface register.
 *
 * @param[in] offset Register offset.
 * @return The register value.
 */
static inline uint32_t GiccRead(const uint32_t offset) { return gicc_base[offset >> 2]; }

bool ArchIrqReady(void) { return gicd_base && gicc_base; }

uint32_t ArchIrqPriorityMask(void) { return GiccRead(GICC_PMR); }

bool ArchIrqIsEnabled(uint32_t line)
{
    if (line >= MAX_IRQS)
        return false;
    return (GicdRead(GICD_ISENABLER + ((line / 32U) * 4U)) >> (line % 32U)) & 1U;
}

bool ArchIrqIsPending(uint32_t line)
{
    if (line >= MAX_IRQS)
        return false;
    return (GicdRead(GICD_ISPENDER + ((line / 32U) * 4U)) >> (line % 32U)) & 1U;
}

uint32_t ArchIrqFirstDeviceLine(void) { return 16U; }

void GicInit(uintptr_t gicd_base_addr, uintptr_t gicc_base_addr)
{

    gicd_base = (volatile uint32_t *)gicd_base_addr;
    gicc_base = (volatile uint32_t *)gicc_base_addr;

    GicdWrite(GICD_CTLR, 0x0);
    GiccWrite(GICC_CTLR, 0x0);

    for (uint32_t i = 0; i < 8; i++) {
        GicdWrite(GICD_IGROUPR + (i * 4), 0x00000000U);
    }

    for (uint32_t reg = 0; reg < 64; reg++) {
        GicdWrite(GICD_IPRIORITYR + (reg * 4), 0xA0A0A0A0);
    }

    for (uint32_t reg = 8; reg < 64; reg++) {
        GicdWrite(GICD_ITARGETSR + (reg * 4), 0x01010101);
    }

    GicdWrite(GICD_CTLR, 0x1);
    GiccWrite(GICC_PMR, 0xFF);
    GiccWrite(GICC_CTLR, 0x1);
}

void GicV2ConfigureIrq(Irq irq_id)
{
    if (irq_id >= 32) {
        uint32_t cfg_off = GICD_ICFGR + ((irq_id / 16) * 4);
        uint32_t shift = ((irq_id % 16) * 2) + 1;
        uint32_t cfg = GicdRead(cfg_off);
        cfg &= ~(1U << shift);
        GicdWrite(cfg_off, cfg);
    }

    if (irq_id >= 32) {
        uint32_t reg_offset = GICD_ITARGETSR + (irq_id & ~3U);
        uint32_t byte_shift = (irq_id % 4) * 8;

        uint32_t val = GicdRead(reg_offset);
        val &= ~(0xFFU << byte_shift);
        val |= (0x01U << byte_shift);
        GicdWrite(reg_offset, val);
    }
}

void GicV2SetPriority(Irq irq_id, uint8_t priority)
{
    uint32_t reg_offset = GICD_IPRIORITYR + (irq_id & ~3U);
    uint32_t byte_shift = (irq_id % 4) * 8;

    uint32_t val = GicdRead(reg_offset);
    val &= ~(0xFFU << byte_shift);
    val |= ((uint32_t)priority << byte_shift);
    GicdWrite(reg_offset, val);
}

void GicV2UnmaskIrq(Irq irq_id)
{
    GicdWrite(GICD_ISENABLER + ((irq_id / 32) * 4), (1 << (irq_id % 32)));
}

void GicV2MaskIrq(Irq irq_id)
{
    GicdWrite(GICD_ICENABLER + ((irq_id / 32) * 4), (1 << (irq_id % 32)));
}

uint32_t GicAcknowledge(void)
{
    return GiccRead(
        GICC_IAR);
}

void GicEnd(uint32_t iar)
{
    
    ArchDsbSy();
    GiccWrite(GICC_EOIR, iar);
}

#include "core/panic.h"
#include "drivers/driver.h"
#include "kernel/mm/vmm/vmm.h"

#define LOG_FMT(fmt) "(board) " fmt
#include "core/log.h"

/**
 * @brief Maps the GIC described by the device tree and initializes it; panics if it cannot be
 * mapped.
 *
 * @param[in] dev Device tree node.
 */
static void GicV2Probe(const FdtDevice *dev)
{
    uint64_t gicd = dev->phys, s_d = dev->size;
    uint64_t gicc = 0, s_c = 0;

    if (dev->nregs >= 2) {
        gicc = dev->phys2;
        s_c = dev->size2;
    }

    void *gicd_va = IoRemap((PhysAddr)gicd, (size_t)s_d);
    void *gicc_va = IoRemap((PhysAddr)(gicc ? gicc : gicd), (size_t)(s_c ? s_c : s_d));

    KDEBUG("GICv2 (GICD) re-mapped to %p", gicd_va);
    if (!gicd_va || !gicc_va)
        panic("Failed to ioremap GIC");

    GicInit((uintptr_t)gicd_va, (uintptr_t)gicc_va);
}

/** @brief Device tree compatible strings this driver binds to. */
static const char *const GIC_COMPAT[] = {"arm,gic-400", "arm,cortex-a15-gic", "arm,gic-v2", NULL};

ZUZU_DRIVER(gicv2, ZUZU_DRV_IRQCHIP) = {
    .name = "GIC",
    .compat = GIC_COMPAT,
    .required = true,
    .probe = GicV2Probe,
};

// gicv2.c - ARM Generic Interrupt Controller v2 implementation

#include "arch/arm/include/gicv2.h"
#include <arch/barrier.h>
#include <arch/irq.h>
#include <stdbool.h>
#include <types.h>

volatile uint32_t *gicd_base, *gicc_base;

/**
 * Helper to write to GICD.
 */
static inline void GicdWrite(uint32_t off, uint32_t val) { gicd_base[off >> 2] = val; }

/**
 * Helper to read from GICD.
 */
static inline uint32_t GicdRead(uint32_t off) { return gicd_base[off >> 2]; }

/**
 * Helper to write to GICC.
 */
static inline void GiccWrite(uint32_t off, uint32_t val) { gicc_base[off >> 2] = val; }

/**
 * Helper to read from GICC.
 */
static inline uint32_t GiccRead(const uint32_t offset) { return gicc_base[offset >> 2]; }

/* ---- Controller introspection (arch/irq.h contract) --------------------- */

bool ArchIrqReady(void) { return gicd_base && gicc_base; }

uint32_t ArchIrqPriorityMask(void) { return GiccRead(GICC_PMR); }

uint32_t ArchIrqEnabledWord(uint32_t word) { return GicdRead(GICD_ISENABLER + (word * 4U)); }

uint32_t ArchIrqPendingWord(uint32_t word) { return GicdRead(GICD_ISPENDER + (word * 4U)); }

void GicInit(uintptr_t gicd_base_addr, uintptr_t gicc_base_addr)
{

    gicd_base = (volatile uint32_t *)gicd_base_addr;
    gicc_base = (volatile uint32_t *)gicc_base_addr;

    // Disable first
    GicdWrite(GICD_CTLR, 0x0);
    GiccWrite(GICC_CTLR, 0x0);

    // Put all interrupts into Group 1 (non-secure)
    // For 256 IRQs => 256/32 = 8 registers
    for (uint32_t i = 0; i < 8; i++) {
        GicdWrite(GICD_IGROUPR + (i * 4), 0x00000000U); // Group 0 (secure)
    }

    // Set priorities for *all* interrupts (including SGI/PPI 0-31)
    // 256 IRQs => IPRIORITYR regs are 4 IRQs per word => 256/4 = 64 words
    for (uint32_t reg = 0; reg < 64; reg++) {
        GicdWrite(GICD_IPRIORITYR + (reg * 4), 0xA0A0A0A0);
    }

    // Target CPU0 for SPIs only (32+). ITARGETSR[0..7] are SGI/PPI and are banked/read-only-ish.
    for (uint32_t reg = 8; reg < 64; reg++) {
        GicdWrite(GICD_ITARGETSR + (reg * 4), 0x01010101);
    }

    // Enable both groups in Distributor and CPU interface
    GicdWrite(GICD_CTLR, 0x1); // EnableGrp0 | EnableGrp1
    GiccWrite(GICC_PMR, 0xFF); // allow all priorities
    GiccWrite(GICC_CTLR, 0x1); // EnableGrp0 | EnableGrp1
}

void GicV2ConfigureIrq(Irq irq_id)
{
    // For SPIs, force level-triggered semantics (ICFGR bit[2n+1] = 0)
    // rather than relying on firmware/reset defaults.
    if (irq_id >= 32) {
        uint32_t cfg_off = GICD_ICFGR + ((irq_id / 16) * 4);
        uint32_t shift = ((irq_id % 16) * 2) + 1;
        uint32_t cfg = GicdRead(cfg_off);
        cfg &= ~(1U << shift);
        GicdWrite(cfg_off, cfg);
    }

    // For SPIs (irq >= 32), set target to CPU0
    if (irq_id >= 32) {
        // ITARGETSR: 1 byte per IRQ at offset 0x800
        uint32_t reg_offset = GICD_ITARGETSR + (irq_id & ~3U); // 4-byte aligned
        uint32_t byte_shift = (irq_id % 4) * 8;

        uint32_t val = GicdRead(reg_offset);
        val &= ~(0xFFU << byte_shift); // Clear this IRQ's byte
        val |= (0x01U << byte_shift);  // Set CPU0 as target
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
    // Enable the interrupt
    GicdWrite(GICD_ISENABLER + ((irq_id / 32) * 4), (1 << (irq_id % 32)));
}

void GicV2MaskIrq(Irq irq_id)
{
    GicdWrite(GICD_ICENABLER + ((irq_id / 32) * 4), (1 << (irq_id % 32))); // Write 1 to disable
}

uint32_t GicAcknowledge(void)
{
    return GiccRead(
        GICC_IAR); // Returns raw IAR value, caller must extract INTID and check for spurious (1023)
}

void GicEnd(uint32_t iar)
{
    /* dsb sy, not ish: EOIR is a device write, and the Inner Shareable domain
     * does not order Device memory. The driver's interrupt-clearing writes must
     * have completed before we signal EOIR, or the GIC still sees the line
     * asserted and re-delivers. QEMU models neither domain, so this is
     * invisible there and only bites on real silicon. */
    ArchDsbSy();
    GiccWrite(GICC_EOIR, iar); // Signal end of interrupt
}

#include "core/panic.h"
#include "drivers/driver.h"
#include "kernel/mm/vmm/vmm.h"

#define LOG_FMT(fmt) "(board) " fmt
#include "core/log.h"

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

static const char *const GIC_COMPAT[] = {"arm,gic-400", "arm,cortex-a15-gic", "arm,gic-v2", NULL};

ZUZU_DRIVER(gicv2, ZUZU_DRV_IRQCHIP) = {
    .name = "GIC",
    .compat = GIC_COMPAT,
    .required = true,
    .probe = GicV2Probe,
};

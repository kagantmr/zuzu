// gicv2.c - ARM Generic Interrupt Controller v2 implementation

#include "arch/arm/include/gicv2.h"

volatile uint32_t *gicd_base, *gicc_base;

/**
 * Helper to write to GICD.
 */
static inline void gicd_write(uint32_t off, uint32_t val)
{
    gicd_base[off >> 2] = val;
}

/**
 * Helper to read from GICD.
 */
static inline uint32_t gicd_read(uint32_t off)
{
    return gicd_base[off >> 2];
}

/**
 * Helper to write to GICC.
 */
static inline void gicc_write(uint32_t off, uint32_t val)
{
    gicc_base[off >> 2] = val;
}

/**
 * Helper to read from GICC.
 */
static inline uint32_t gicc_read(const uint32_t offset)
{
    return gicc_base[offset >> 2];
}

void gic_init(uintptr_t gicd_base_addr, uintptr_t gicc_base_addr) {

    gicd_base = (volatile uint32_t *)gicd_base_addr;
    gicc_base = (volatile uint32_t *)gicc_base_addr;

    // Disable first
    gicd_write(GICD_CTLR, 0x0);
    gicc_write(GICC_CTLR, 0x0);

    // Put all interrupts into Group 1 (non-secure)
    // For 256 IRQs => 256/32 = 8 registers
    for (uint32_t i = 0; i < 8; i++) {
        gicd_write(GICD_IGROUPR + i * 4, 0x00000000u); // Group 0 (secure)
    }

    // Set priorities for *all* interrupts (including SGI/PPI 0-31)
    // 256 IRQs => IPRIORITYR regs are 4 IRQs per word => 256/4 = 64 words
    for (uint32_t reg = 0; reg < 64; reg++) {
        gicd_write(GICD_IPRIORITYR + reg * 4, 0xA0A0A0A0);
    }

    // Target CPU0 for SPIs only (32+). ITARGETSR[0..7] are SGI/PPI and are banked/read-only-ish.
    for (uint32_t reg = 8; reg < 64; reg++) {
        gicd_write(GICD_ITARGETSR + reg * 4, 0x01010101);
    }

    // Enable both groups in Distributor and CPU interface
    gicd_write(GICD_CTLR, 0x1);   // EnableGrp0 | EnableGrp1
    gicc_write(GICC_PMR, 0xFF);   // allow all priorities
    gicc_write(GICC_CTLR, 0x1);   // EnableGrp0 | EnableGrp1
}

void gic_enable_irq(uint32_t irq_id) {
    // For SPIs, force level-triggered semantics (ICFGR bit[2n+1] = 0)
    // rather than relying on firmware/reset defaults.
    if (irq_id >= 32) {
        uint32_t cfg_off = GICD_ICFGR + (irq_id / 16) * 4;
        uint32_t shift = ((irq_id % 16) * 2) + 1;
        uint32_t cfg = gicd_read(cfg_off);
        cfg &= ~(1u << shift);
        gicd_write(cfg_off, cfg);
    }

    // Enable the interrupt
    gicd_write(GICD_ISENABLER + (irq_id / 32) * 4, (1 << (irq_id % 32)));
    
    // For SPIs (irq >= 32), set target to CPU0
    if (irq_id >= 32) {
        // ITARGETSR: 1 byte per IRQ at offset 0x800
        uint32_t reg_offset = GICD_ITARGETSR + (irq_id & ~3);  // 4-byte aligned
        uint32_t byte_shift = (irq_id % 4) * 8;
        
        uint32_t val = gicd_read(reg_offset);
        val &= ~(0xFF << byte_shift);      // Clear this IRQ's byte
        val |= (0x01 << byte_shift);       // Set CPU0 as target
        gicd_write(reg_offset, val);
    }
}

void gic_disable_irq(uint32_t irq_id) {
    gicd_write(GICD_ICENABLER + (irq_id / 32) * 4, (1 << (irq_id % 32)));  // Write 1 to disable
}


uint32_t gic_acknowledge(void) {
    return gicc_read(GICC_IAR); // Returns raw IAR value, caller must extract INTID and check for spurious (1023)
}


void gic_end(uint32_t iar) {
    gicc_write(GICC_EOIR, iar); // Signal end of interrupt
}
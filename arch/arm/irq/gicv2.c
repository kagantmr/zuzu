#include "arch/arm/include/gicv2.h"

volatile uint32_t* gicd_base;
volatile uint32_t* gicc_base;

static inline void gicd_write(uint32_t off, uint32_t val)
{
    gicd_base[off >> 2] = val;
}

static inline uint32_t gicd_read(uint32_t off)
{
    return gicd_base[off >> 2];
}

static inline void gicc_write(uint32_t off, uint32_t val)
{
    gicc_base[off >> 2] = val;
}

static inline uint32_t gicc_read(uint32_t off)
{
    return gicc_base[off >> 2];
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
    gicd_write(GICD_ISENABLER + (irq_id / 32) * 4, (1 << (irq_id % 32)));
}

void gic_disable_irq(uint32_t irq_id) {
    gicd_write(GICD_ICENABLER + (irq_id / 32) * 4, (1 << (irq_id % 32)));
}


uint32_t gic_acknowledge(void) {
    return gicc_read(GICC_IAR);
}


void gic_end(uint32_t iar) {
    gicc_write(GICC_EOIR, iar); // Signal end of interrupt
}
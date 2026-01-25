#include "arch/arm/include/irq.h"
#include "core/log.h"

#include <stdint.h>
#include <stddef.h>


#define TIMER_IRQ_PHYS 30  // CNTP (Physical Non-secure) timer PPI
#define TIMER_IRQ_VIRT 27  // CNTV (Virtual) timer PPI (some QEMU setups deliver this)

static inline uint32_t read_cntfrq(void) {
    uint32_t v;
    __asm__ volatile("mrc p15, 0, %0, c14, c0, 0" : "=r"(v));
    return v;
}

static inline void write_cntp_tval(uint32_t v) {
    __asm__ volatile("mcr p15, 0, %0, c14, c2, 0" :: "r"(v));
    __asm__ volatile("isb");
}

static inline void write_cntp_ctl(uint32_t v) {
    __asm__ volatile("mcr p15, 0, %0, c14, c2, 1" :: "r"(v));
    __asm__ volatile("isb");
}

static inline void write_cntv_tval(uint32_t v) {
    __asm__ volatile("mcr p15, 0, %0, c14, c3, 0" :: "r"(v));
    __asm__ volatile("isb");
}

static inline void write_cntv_ctl(uint32_t v) {
    __asm__ volatile("mcr p15, 0, %0, c14, c3, 1" :: "r"(v));
    __asm__ volatile("isb");
}

static volatile uint64_t tick_count = 0;

static void timer_handler(void* ctx) {
    (void)ctx;
    tick_count++;

    if ((tick_count % 100) == 0) {
        uint32_t lo = (uint32_t)tick_count;
        uint32_t hi = (uint32_t)(tick_count >> 32);
        KINFO("Timer tick: %u%u", hi, lo);
    }

    // Reload both physical and virtual timers so whichever one is wired keeps ticking
    uint32_t freq = read_cntfrq();
    uint32_t tval = freq / 100;  // 10ms interval

    write_cntp_tval(tval);
    write_cntv_tval(tval);
}

void timer_init(void) {
    // Read counter frequency
    uint32_t freq = read_cntfrq();
    KINFO("Timer frequency: %u Hz", freq);

    // Register handler for both PPIs
    irq_register(TIMER_IRQ_PHYS, timer_handler, NULL);
    irq_register(TIMER_IRQ_VIRT, timer_handler, NULL);

    // Enable IRQ lines in GIC
    irq_enable_line(TIMER_IRQ_PHYS);
    irq_enable_line(TIMER_IRQ_VIRT);

    // Set timer value (10ms = freq/100)
    uint32_t tval = freq / 100;
    write_cntp_tval(tval);
    write_cntv_tval(tval);

    // Enable timers: ENABLE=1, IMASK=0
    uint32_t ctl = 1;
    write_cntp_ctl(ctl);
    write_cntv_ctl(ctl);

    KINFO("Generic timer initialized (10ms interval, CNTP+CNTV)");
}
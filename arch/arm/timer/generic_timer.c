#include "arch/arm/include/irq.h"
#include "arch/arm/timer/generic_timer.h"
#include "kernel/time/tick.h"
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

static uint32_t freq = 0;
static uint32_t tval = 0;

static void generic_timer_handler(void* ctx) {
    (void)ctx;
    
    write_cntp_tval(tval);
    write_cntv_tval(tval);

    tick_announce();
}

void generic_timer_init(void) {
    // Read counter frequency
    freq = read_cntfrq();
    tval = freq / 100;  // 10ms interval

    // Register handler for both PPIs
    irq_register(TIMER_IRQ_PHYS, generic_timer_handler, NULL);
    irq_register(TIMER_IRQ_VIRT, generic_timer_handler, NULL);

    // Enable IRQ lines in GIC
    irq_enable_line(TIMER_IRQ_PHYS);
    irq_enable_line(TIMER_IRQ_VIRT);

    // Set timer value (10ms = freq/100)
    write_cntp_tval(tval);
    write_cntv_tval(tval);

    // Enable timers: ENABLE=1, IMASK=0
    uint32_t ctl = 1;
    write_cntp_ctl(ctl);
    write_cntv_ctl(ctl);

    KDEBUG("Generic timer initialized (10ms interval, %u Hz)", freq);
}

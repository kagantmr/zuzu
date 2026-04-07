#include "arch/arm/include/irq.h"
#include "arch/arm/timer/generic_timer.h"
#include "kernel/time/tick.h"
#include "core/log.h"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

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
static bool timer_first_irq_logged = false;
static bool timer_phys_seen = false;
static bool timer_virt_seen = false;
static bool timer_dual_source_warned = false;

static void generic_timer_handler(void* ctx) {
    uint32_t irq_id = (uint32_t)(uintptr_t)ctx;

    if (irq_id == TIMER_IRQ_PHYS)
        timer_phys_seen = true;
    else if (irq_id == TIMER_IRQ_VIRT)
        timer_virt_seen = true;

    if (!timer_first_irq_logged) {
        const char *src = (irq_id == TIMER_IRQ_PHYS) ? "CNTP (phys)" :
                          (irq_id == TIMER_IRQ_VIRT) ? "CNTV (virt)" : "unknown";
        KDEBUG("Generic timer first IRQ source: %s (irq=%u)", src, irq_id);
        timer_first_irq_logged = true;
    }

    if (timer_phys_seen && timer_virt_seen && !timer_dual_source_warned) {
        KDEBUG("Both CNTP and CNTV timer IRQs are firing; tick rate may effectively double");
        timer_dual_source_warned = true;
    }
    
    //write_cntp_tval(tval);
    write_cntv_tval(tval);

    tick_announce();
}

void generic_timer_init(void) {
    // Read counter frequency
    freq = read_cntfrq();
    tval = freq / 100;  // 10ms interval

    // Register handler for both PPIs
    irq_register(TIMER_IRQ_PHYS, generic_timer_handler, (void *)(uintptr_t)TIMER_IRQ_PHYS);
    irq_register(TIMER_IRQ_VIRT, generic_timer_handler, (void *)(uintptr_t)TIMER_IRQ_VIRT);

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

    //KDEBUG("Generic timer initialized (10ms interval, %u Hz)", freq);
}

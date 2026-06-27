// generic_timer.c - ARMv7-A generic timer implementation

#include <arch/irq.h>
#include <arch/timer.h>
#include "arch/arm/timer/generic_timer.h"
#include "kernel/time/tick.h"
#include <zuzu/types.h>
#include <stdint.h>


/**
 * @brief Read the counter frequency from the CNTFRQ register.
 * @return Counter frequency in Hz.
 */
static inline uint32_t read_cntfrq(void)
{
    uint32_t v;
    __asm__ volatile("mrc p15, 0, %0, c14, c0, 0" : "=r"(v));
    return v;
}

/**
 * @brief Write to the CNTP_TVAL register to set the timer interval.
 * @param v Value to write (number of ticks until next interrupt).
 */
static inline void write_cntp_tval(uint32_t v)
{
    __asm__ volatile("mcr p15, 0, %0, c14, c2, 0" ::"r"(v));
    __asm__ volatile("isb");
}

/**
 * @brief Write to the CNTP_CTL register to enable/disable the physical timer.
 * @param v Control value (bit 0 = enable, bit 1 = interrupt mask).
 */
static inline void write_cntp_ctl(uint32_t v)
{
    __asm__ volatile("mcr p15, 0, %0, c14, c2, 1" ::"r"(v));
    __asm__ volatile("isb");
}

/**
 * @brief Write to the CNTV_TVAL register to set the virtual timer interval.
 * @param v Value to write (number of ticks until next interrupt).
 */
static inline void write_cntv_tval(uint32_t v)
{
    __asm__ volatile("mcr p15, 0, %0, c14, c3, 0" ::"r"(v));
    __asm__ volatile("isb");
}

/**
 * @brief Write to the CNTV_CTL register to enable/disable the virtual timer.
 * @param v Control value (bit 0 = enable, bit 1 = interrupt mask
 * Note: enabling the virtual timer may cause it to fire alongside the physical timer if both are present, which can effectively double the tick rate.
 */
static inline void write_cntv_ctl(uint32_t v)
{
    __asm__ volatile("mcr p15, 0, %0, c14, c3, 1" ::"r"(v));
    __asm__ volatile("isb");
}

static uint32_t freq = 0;
static uint32_t tval = 0;

static void generic_timer_handler(void *ctx)
{
    (void)ctx;
    write_cntv_tval(tval);
    tick_announce();
}

void arch_timer_init(void)
{
    freq = read_cntfrq();
    tval = freq / 100;

    /* Silence CNTP at the source so its interrupt line stays deasserted */
    write_cntp_ctl(0x2); /* ENABLE=0, IMASK=1 */

    arch_irq_register(TIMER_IRQ_VIRT, generic_timer_handler, NULL);
    arch_irq_enable_line(TIMER_IRQ_VIRT);

    write_cntv_tval(tval);
    write_cntv_ctl(1); /* ENABLE=1, IMASK=0 */
}

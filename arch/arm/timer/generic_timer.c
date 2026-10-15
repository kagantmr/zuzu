// generic_timer.c - ARMv7-A generic timer implementation

#include "arch/arm/timer/generic_timer.h"
#include <arch/irq.h>
#include <arch/timer.h>
#include <stdint.h>
#include <types.h>

/**
 * @brief Read the counter frequency from the CNTFRQ register.
 * @return Counter frequency in Hz.
 */
static inline uint32_t ReadCntFrq(void)
{
    uint32_t v;
    __asm__ volatile("mrc p15, 0, %0, c14, c0, 0" : "=r"(v));
    return v;
}

/**
 * @brief Write to the CNTP_CTL register to enable/disable the physical timer.
 * @param v Control value (bit 0 = enable, bit 1 = interrupt mask).
 */
static inline void WriteCntpCtl(uint32_t v)
{
    __asm__ volatile("mcr p15, 0, %0, c14, c2, 1" ::"r"(v));
    __asm__ volatile("isb");
}

static uint32_t cntv_ctl_shadow = ~0U;

/**
 * @brief Write to the CNTV_CTL register to enable/disable the virtual timer.
 * @param v Control value (bit 0 = enable, bit 1 = interrupt mask
 * Note: enabling the virtual timer may cause it to fire alongside the physical timer if both are
 * present, which can effectively double the tick rate.
 */
static inline void WriteCntvCtl(uint32_t v)
{
    if (v == cntv_ctl_shadow)
        return;
    cntv_ctl_shadow = v;
    __asm__ volatile("mcr p15, 0, %0, c14, c3, 1" ::"r"(v));
    __asm__ volatile("isb");
}

/* Monotonic anchor for the workaround below. Single-core; racy across the
 * IRQ boundary only to the extent of a torn 64-bit load, which the clamp
 * tolerates and the next call heals. */
static uint64_t cnt_last;

static inline uint64_t ReadCntvct(void)
{
    uint64_t v;
    __asm__ volatile("isb\n mrrc p15, 1, %Q0, %R0, c14" : "=r"(v)::"memory");
    if (v < cnt_last)
        v = cnt_last;
    cnt_last = v;
    return v;
}

static uint32_t freq = 0;

static void (*expire_handler)(void);

static void ArmGenericTimerHandler(void *ctx)
{
    (void)ctx;
    WriteCntvCtl(0x3);
    if (expire_handler)
        expire_handler();
}

static inline void EnableUserCounterRead(void)
{
    uint32_t v;
    __asm__ volatile("mrc p15, 0, %0, c14, c1, 0" : "=r"(v));   /* CNTKCTL */
    v &= ~0x303U;   /* PL0PCTEN, PL0VCTEN, PL0VTEN, PL0PTEN off */
    v |= 0x2U;      /* PL0VCTEN: user may read CNTVCT, nothing else */
    __asm__ volatile("mcr p15, 0, %0, c14, c1, 0" ::"r"(v));
    __asm__ volatile("isb");
}

void ArchTimerInit(void)
{
    freq = ReadCntFrq();
    EnableUserCounterRead();

    /* Silence CNTP at the source so its interrupt line stays deasserted */
    WriteCntpCtl(0x2); /* ENABLE=0, IMASK=1 */

    ArchIrqRegister(TIMER_IRQ_VIRT, ArmGenericTimerHandler, NULL);
    ArchIrqSetPrio(TIMER_IRQ_VIRT, 0x80);
    ArchIrqUnmaskLine(TIMER_IRQ_VIRT);

    /* Enabled + masked; SchedArmTimer() programs CVAL and unmasks on the
     * first Schedule(). Until then nothing needs a timer wakeup. */
    WriteCntvCtl(0x3); /* ENABLE=1, IMASK=1 */


}

void ArchTimerSetHandler(void (*on_expire)(void)) { expire_handler = on_expire; }

Time ArchTimerNow(void) { return ReadCntvct(); }
uint32_t ArchTimerFreq(void) { return freq; }
static uint64_t cntv_cval_shadow = ~0ULL; /* sentinel: no real deadline is ever this value */

void ArchTimerSetDeadline(Time abs_count)
{
    if (abs_count != cntv_cval_shadow) {
        cntv_cval_shadow = abs_count;
        __asm__ volatile("mcrr p15, 3, %0, %1, c14" ::"r"((uint32_t)abs_count),
                         "r"((uint32_t)(abs_count >> 32)));
        __asm__ volatile("isb");
    }
    WriteCntvCtl(0x1); /* ENABLE=1, IMASK=0 already shadow-gated on its own */
}

void ArchTimerDisable(void) { WriteCntvCtl(0x2); /* IMASK=1 */ }

#include "drivers/driver.h"

#define LOG_FMT(fmt) "(board) " fmt
#include "core/log.h"

static void GenericTimerProbe(const FdtDevice *dev)
{
    (void)dev;
    KDEBUG("Using ARM generic timer as tick source");
    ArchTimerInit();
}

ZUZU_DRIVER(generic_timer, ZUZU_DRV_TIMER) = {
    .name = "ARM generic timer",
    .compat = NULL,
    .required = false,
    .probe = GenericTimerProbe,
};

/**
 * @file
 * @brief ARM generic timer: the virtual timer as the kernel's one-shot deadline.
 */

#include "arch/arm/timer/generic_timer.h"
#include <arch/irq.h>
#include <arch/timer.h>
#include <stdint.h>
#include <types.h>

/**
 * @brief Reads the counter frequency (CNTFRQ).
 *
 * @return Ticks per second.
 */
static inline uint32_t ReadCntFrq(void)
{
    uint32_t v;
    __asm__ volatile("mrc p15, 0, %0, c14, c0, 0" : "=r"(v));
    return v;
}

/**
 * @brief Writes the physical timer control register (CNTP_CTL).
 *
 * @param[in] v New value.
 */
static inline void WriteCntpCtl(uint32_t v)
{
    __asm__ volatile("mcr p15, 0, %0, c14, c2, 1" ::"r"(v));
    __asm__ volatile("isb");
}

/** @brief Last value written to CNTV_CTL, to skip redundant writes. */
static uint32_t cntv_ctl_shadow = ~0U;

/**
 * @brief Writes the virtual timer control register (CNTV_CTL) if it changed.
 *
 * Bit 0 enables the timer, bit 1 masks its interrupt.
 *
 * @param[in] v New value.
 */
static inline void WriteCntvCtl(uint32_t v)
{
    if (v == cntv_ctl_shadow)
        return;
    cntv_ctl_shadow = v;
    __asm__ volatile("mcr p15, 0, %0, c14, c3, 1" ::"r"(v));
    __asm__ volatile("isb");
}

/** @brief Highest count read so far. */
static uint64_t cnt_last;

/**
 * @brief Reads the virtual count, never returning less than a previous read.
 *
 * Some QEMU versions return out-of-order values; clamping keeps time monotonic.
 *
 * @return The counter value.
 */
static inline uint64_t ReadCntvct(void)
{
    uint64_t v;
    __asm__ volatile("isb\n mrrc p15, 1, %Q0, %R0, c14" : "=r"(v)::"memory");
    if (v < cnt_last)
        v = cnt_last;
    cnt_last = v;
    return v;
}

/** @brief Counter frequency, read at init. */
static uint32_t freq = 0;

/** @brief Called when a deadline passes. */
static void (*expire_handler)(void);

/**
 * @brief Timer interrupt: masks the timer and runs the expiry handler.
 *
 * @param[in] ctx Unused.
 */
static void ArmGenericTimerHandler(void *ctx)
{
    (void)ctx;
    WriteCntvCtl(0x3);
    if (expire_handler)
        expire_handler();
}

/** @brief Lets user code read the virtual count, and nothing else (CNTKCTL). */
static inline void EnableUserCounterRead(void)
{
    uint32_t v;
    __asm__ volatile("mrc p15, 0, %0, c14, c1, 0" : "=r"(v));
    v &= ~0x303U;
    v |= 0x2U;
    __asm__ volatile("mcr p15, 0, %0, c14, c1, 0" ::"r"(v));
    __asm__ volatile("isb");
}

void ArchTimerInit(void)
{
    freq = ReadCntFrq();
    EnableUserCounterRead();

    WriteCntpCtl(0x2);

    ArchIrqRegister(TIMER_IRQ_VIRT, ArmGenericTimerHandler, NULL);
    ArchIrqSetPrio(TIMER_IRQ_VIRT, 0x80);
    ArchIrqUnmaskLine(TIMER_IRQ_VIRT);

    WriteCntvCtl(0x3);

}

void ArchTimerSetHandler(void (*on_expire)(void)) { expire_handler = on_expire; }

Time ArchTimerNow(void) { return ReadCntvct(); }
uint32_t ArchTimerFreq(void) { return freq; }
/** @brief Last value written to CNTV_CVAL, to skip redundant writes. */
static uint64_t cntv_cval_shadow = ~0ULL;

void ArchTimerSetDeadline(Time abs_count)
{
    if (abs_count != cntv_cval_shadow) {
        cntv_cval_shadow = abs_count;
        __asm__ volatile("mcrr p15, 3, %0, %1, c14" ::"r"((uint32_t)abs_count),
                         "r"((uint32_t)(abs_count >> 32)));
        __asm__ volatile("isb");
    }
    WriteCntvCtl(0x1);
}

void ArchTimerDisable(void) { WriteCntvCtl(0x2);  }

#include "drivers/driver.h"

#define LOG_FMT(fmt) "(board) " fmt
#include "core/log.h"

/**
 * @brief Driver probe: initializes the timer.
 *
 * @param[in] dev Device tree node; unused.
 */
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

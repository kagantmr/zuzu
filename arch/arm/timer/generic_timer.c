// generic_timer.c - ARMv7-A generic timer implementation

#include "arch/arm/timer/generic_timer.h"
#include "kernel/time/tick.h"
#include <arch/irq.h>
#include <arch/timer.h>
#include <stdint.h>
#include <zuzu/types.h>

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

/*
static inline void write_cntp_tval(uint32_t v)
{
    __asm__ volatile("mcr p15, 0, %0, c14, c2, 0" ::"r"(v));
    __asm__ volatile("isb");
}
*/

/**
 * @brief Write to the CNTP_CTL register to enable/disable the physical timer.
 * @param v Control value (bit 0 = enable, bit 1 = interrupt mask).
 */
static inline void WriteCntpCtl(uint32_t v)
{
    __asm__ volatile("mcr p15, 0, %0, c14, c2, 1" ::"r"(v));
    __asm__ volatile("isb");
}


/**
 * @brief Write to the CNTV_CTL register to enable/disable the virtual timer.
 * @param v Control value (bit 0 = enable, bit 1 = interrupt mask
 * Note: enabling the virtual timer may cause it to fire alongside the physical timer if both are
 * present, which can effectively double the tick rate.
 */
static inline void WriteCntvCtl(uint32_t v)
{
    __asm__ volatile("mcr p15, 0, %0, c14, c3, 1" ::"r"(v));
    __asm__ volatile("isb");
}

/* Monotonic anchor for the workaround below. Single-core; racy across the
 * IRQ boundary only to the extent of a torn 64-bit load, which the clamp
 * tolerates and the next call heals. */
static uint64_t cnt_last;

static inline uint64_t ReadCntvct(void)
{
    /* CNTPCT (physical count). QEMU's TCG generic-timer model intermittently
     * returns this register with the halves misplaced -- the real count in
     * the high word, low word zero (a ~2^32 forward jump), or both words
     * zero. It happens reliably enough (~1 in 6, worse from exception
     * context) to poison every deadline derived from it. Two mitigations:
     *   - if the low word is 0 and the high word isn't, it's the swap: undo it
     *   - clamp against a monotonic anchor: real advance between any two
     *     kernel reads is far below one second (~2^26 counts), so anything
     *     outside [last, last + 2^26) is the glitch -> pin to last.
     */
    uint64_t v;
    __asm__ volatile("mrrc p15, 0, %Q0, %R0, c14" : "=r"(v) :: "memory");

    /* Swap glitch: real count in the high word, low word zero. Undo it. */
    if ((uint32_t)v == 0 && (v >> 32) != 0)
        v >>= 32;
    /* The counter is strictly monotonic; a value below the last one we
     * returned (covers the words-zeroed glitch too) is bogus -- pin it.
     * A large *forward* jump is legitimate after a long WFI, so never clamp
     * those. */
    if (v < cnt_last)
        v = cnt_last;
    cnt_last = v;
    return v;
}

static uint32_t freq = 0;

static void ArmGenericTimerHandler(void *ctx)
{
    (void)ctx;
    /* One-shot: no TVAL reload. Mask our own line so we don't re-fire on the
     * same expired CVAL before the scheduler re-arms; SchedArmTimer() (run
     * from schedule() on the IRQ-return path) sets the next deadline and
     * clears IMASK again. tick_announce keeps uptime / any tick callback
     * alive -- it just no longer paces the scheduler. */
    WriteCntvCtl(0x3); /* ENABLE=1, IMASK=1 */
    tick_announce();
}

void ArchTimerInit(void)
{
    freq = ReadCntFrq();

    /* Silence CNTP at the source so its interrupt line stays deasserted */
    WriteCntpCtl(0x2); /* ENABLE=0, IMASK=1 */

    arch_irq_register(TIMER_IRQ_VIRT, ArmGenericTimerHandler, NULL);
    arch_irq_enable_line(TIMER_IRQ_VIRT);

    /* Enabled + masked; SchedArmTimer() programs CVAL and unmasks on the
     * first schedule(). Until then nothing needs a timer wakeup. */
    WriteCntvCtl(0x3); /* ENABLE=1, IMASK=1 */
}

Time ArchTimerNow(void) { return ReadCntvct(); }
uint32_t ArchTimerFreq(void) { return freq; }

void ArchTimerSetDeadline(Time abs_count)
{
    /* Program CNTV_CVAL, then enable + unmask. Writing CVAL first means that
     * if abs_count is already in the past the interrupt latches immediately
     * on unmask -- which is the intended behaviour for a missed deadline. */
    __asm__ volatile("mcrr p15, 3, %0, %1, c14"
                     :: "r"((uint32_t)abs_count), "r"((uint32_t)(abs_count >> 32)));
    __asm__ volatile("isb");
    WriteCntvCtl(0x1); /* ENABLE=1, IMASK=0 */
}

void ArchTimerDisable(void) { WriteCntvCtl(0x2); /* IMASK=1 */ }

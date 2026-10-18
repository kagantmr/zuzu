/**
 * @file
 * @brief Architecture-neutral one-shot timer and counter.
 */

#ifndef ARCH_TIMER_H
#define ARCH_TIMER_H

#include <stdint.h>
#include <types.h>

/** @brief Sets up the timer and its interrupt, leaving the timer disarmed. */
void ArchTimerInit(void);
/**
 * @brief Sets the function called, in interrupt context, when an armed deadline passes.
 *
 * @param[in] on_expire Handler, or NULL for none.
 */
void ArchTimerSetHandler(void (*on_expire)(void));

/**
 * @brief Reads the counter.
 *
 * @return The current counter value; never goes backwards.
 */
Time ArchTimerNow(void);
/**
 * @brief Counter frequency.
 *
 * @return Counter ticks per second.
 */
uint32_t ArchTimerFreq(void);

/**
 * @brief Arms the timer to fire once when the counter reaches @p abs_count.
 *
 * A deadline already in the past fires immediately.
 *
 * @param[in] abs_count Absolute counter value.
 */
void ArchTimerSetDeadline(Time abs_count);

/** @brief Disarms the timer. */
void ArchTimerDisable(void);

/**
 * @brief Converts a delay into an absolute counter value for ArchTimerSetDeadline().
 *
 * @param[in] ms Delay in milliseconds.
 * @return The counter value @p ms milliseconds from now.
 */
static inline uint64_t ArchDeadlineFromMs(uint32_t ms)
{
    uint64_t delta = ((uint64_t)ms * (uint64_t)ArchTimerFreq()) / 1000U;
    return (uint64_t)ArchTimerNow() + delta;
}

#endif // ARCH_TIMER_H

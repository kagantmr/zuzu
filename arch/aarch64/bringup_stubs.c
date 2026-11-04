/**
 * @file bringup_stubs.c
 * @brief Temporary AArch64 bring-up stand-ins; delete each one when its real subsystem links.
 */

#include <arch/timer.h>
#include <stdint.h>

/* Until timer/generic_timer.c links (it needs the GIC): raw counter reads for log timestamps. */
Time ArchTimerNow(void)
{
    uint64_t v;
    __asm__ volatile("isb\n mrs %0, cntvct_el0" : "=r"(v)::"memory");
    return v;
}

uint32_t ArchTimerFreq(void)
{
    uint64_t v;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(v));
    return (uint32_t)v;
}

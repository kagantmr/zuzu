/**
 * @file bringup_stubs.c
 * @brief Temporary AArch64 bring-up stand-ins; delete each one when its real subsystem links.
 */

#include "core/kprintf.h"
#include "core/panic.h"
#include "kernel/mm/pmm/pmm_internal.h"
#include <arch/cpu.h>
#include <arch/timer.h>
#include <list.h>
#include <snprintf.h>
#include <stdarg.h>
#include <stdint.h>

/* Until core/panic.c links (it needs tasks, spaces and the PMM): print the reason and halt. */
_Noreturn void panic(const char *fmt, ...)
{
    char buf[256];
    va_list args;

    ArchGlobalIrqDisable();
    va_start(args, fmt);
    (void)vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    kprintf("\nPANIC: %s\n", buf);
    ArchCpuHalt();
}

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

/* Until kernel/mm/pmm/pmm_pressure.c links (it needs IPC events): no pressure subscribers. */
ListHead pmm_subscribers;

void PmmSignalSubscribers(void) {}

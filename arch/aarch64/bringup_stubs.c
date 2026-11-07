/**
 * @file bringup_stubs.c
 * @brief Temporary AArch64 bring-up stand-ins; delete each one when its real subsystem links.
 */

#include "core/kprintf.h"
#include "core/panic.h"
#include "kernel/mm/pmm/pmm_internal.h"
#include <arch/cpu.h>
#include <list.h>
#include <snprintf.h>
#include <stdarg.h>
#include <stdint.h>

PanicFaultContext panic_fault_ctx;

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

void UnhandledException(uint64_t esr, uint64_t elr, uint64_t far);

/* Until the exception handlers are ported: report the fault and halt. */
void UnhandledException(uint64_t esr, uint64_t elr, uint64_t far)
{
    kprintf("\nUNHANDLED EXCEPTION: ESR=%016llx ELR=%016llx FAR=%016llx\n",
            (unsigned long long)esr, (unsigned long long)elr, (unsigned long long)far);
    ArchCpuHalt();
}

/* Until kernel/mm/pmm/pmm_pressure.c links (it needs IPC events): no pressure subscribers. */
ListHead pmm_subscribers;

void PmmSignalSubscribers(void) {}

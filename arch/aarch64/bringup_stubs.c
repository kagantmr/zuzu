/**
 * @file bringup_stubs.c
 * @brief Temporary AArch64 bring-up stand-ins
 */

#include "core/kprintf.h"
#include "core/panic.h"
#include "kernel/irq/irq_relay.h"
#include "kernel/sched/sched.h"
#include "kernel/mm/pmm/pmm_internal.h"
#include <arch/cpu.h>
#include <list.h>
#include <snprintf.h>
#include <stdarg.h>
#include <stdint.h>

/* Until pmm pressure links  */
ListHead pmm_subscribers;

void PmmSignalSubscribers(void) {}

/* Until scheduler links */
TaskObject *current_task;

size_t SchedGetReadyQueue(TaskObject **out, size_t max_out)
{
    (void)out;
    (void)max_out;
    return 0;
}

size_t SchedGetSleepers(TaskObject **out, size_t max_out)
{
    (void)out;
    (void)max_out;
    return 0;
}

/* Until irq relay */
const IrqOwner *GetIrqOwnersList(void) { return NULL; }

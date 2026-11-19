/**
 * @file lbx.c
 * 
 * @brief Implementation of the litterbox task.
 */

#include "lbx.h"
#include "kernel/sched/sched.h"
#include "core/panic.h"

#define LOG_FMT(fmt) "(lbx) " fmt

#include "core/log.h"
#include <arch/platform.h>
#include "kernel/mm/pmm/pmm.h"

static TaskObject *lbx;
static int lbx_pending;
static ListHead lbx_wait_queue = LIST_HEAD_INIT(lbx_wait_queue);



/**
 * @brief Gets rid of the kmain boot stack.
 */
static void LbxFreeBootStack(void)
{
    PhysAddr base, end;
    ArchBootStackRange(&base, &end);
    PmmUnmarkRange(base, end);
    KINFO("freed boot stack, %u KB", (unsigned)((end - base) / 1024));
}

/**
 * @brief Entry point for the litterbox task.
 */
static void LbxMain(void) __attribute__((noreturn));
static void LbxMain(void)
{
    LbxFreeBootStack();
    for (;;) {
        while (lbx_pending > 0) {
            lbx_pending--;
            KDEBUG("step");
            SchedOpenIrqWindow();
            Schedule();
        }
        SchedBlockOn(&lbx_wait_queue, TIMEOUT_INFINITE);
    }
}

void LbxKick(void)
{
    lbx_pending++;
    if (lbx->state == TASK_STATE_BLOCKED) {
        SchedUnblock(lbx);
        SchedAdd(lbx);
    }
}

void LbxInit(void)
{
    lbx = TaskCreateKernel(LbxMain, SCHED_PRIO_DEFAULT);
    if (!lbx)
        panic("lbx: cannot create task");
    SchedAdd(lbx);
}

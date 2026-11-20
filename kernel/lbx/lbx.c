/**
 * @file lbx.c
 *
 * @brief Implementation of the litterbox task.
 */

#include "lbx.h"
#include "core/panic.h"
#include "kernel/mm/pmm/pmm.h"
#include "kernel/sched/sched.h"
#include "kernel/checkpoint/checkpoint.h"
#include <arch/platform.h>

#define LOG_FMT(fmt) "(lbx) " fmt
#include "core/log.h"

/** @brief Most work units one step may do. */
#define LBX_BUDGET CHECKPOINT_PAGES

/** @brief The litterbox task. */
static TaskObject *lbx;
/** @brief Queued objects, one queue per priority level. */
static ListHead lbx_queues[SCHED_PRIORITY_LEVELS];
/** @brief One bit per priority level whose queue is not empty. */
static uint32_t lbx_ready;
/** @brief Where lbx sleeps while every queue is empty. */
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
        while (lbx_ready) {
            Prio top = 31 - (unsigned)__builtin_clz(lbx_ready);
            if (lbx->priority != top)
                SchedSetEffective(lbx, top);
            ListNode *ln = ListPopFront(&lbx_queues[top]);
            if (!ln) {
                lbx_ready &= ~(1U << top);
                continue;
            }
            LbxNode *n = container_of(ln, LbxNode, node);
            if (n->step(n, LBX_BUDGET) == LBX_MORE)
                ListAddTail(&n->node, &lbx_queues[top].node);
            if (ListIsEmpty(&lbx_queues[top]))
                lbx_ready &= ~(1U << top);
            CheckpointTask();
        }
        SchedSetEffective(lbx, 0);
        SchedBlockOn(&lbx_wait_queue, TIMEOUT_INFINITE);
    }
}

void LbxQueue(LbxNode *n, LbxStepFn step)
{
    if (n->node.next)
        return;
    Prio p = current_task ? current_task->priority : 0;
    n->step = step;
    n->prio = p;
    ListAddTail(&n->node, &lbx_queues[p].node);
    lbx_ready |= 1U << p;
    if (lbx->state == TASK_STATE_BLOCKED)
        SchedUnblock(lbx);
    if (p > lbx->priority)
        SchedSetEffective(lbx, p);
    if (lbx->state == TASK_STATE_READY && !lbx->node.next)
        SchedAdd(lbx);
}

void LbxCancel(LbxNode *n)
{
    if (!n->node.next)
        return;
    ListRemove(&n->node);
    if (ListIsEmpty(&lbx_queues[n->prio]))
        lbx_ready &= ~(1U << n->prio);
}

void LbxInit(void)
{
    for (size_t i = 0; i < SCHED_PRIORITY_LEVELS; i++)
        ListInit(&lbx_queues[i]);
    lbx = TaskCreateKernel(LbxMain, 0);
    if (!lbx)
        panic("lbx: cannot create task");
    SchedAdd(lbx);
}

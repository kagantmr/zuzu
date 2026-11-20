/**
 * @file checkpoint.h
 *
 * @brief Checkpoints: spots in long kernel loops where the work stops if an interrupt or a
 * reschedule is waiting.
 *
 * A syscall stops by rewinding itself so it runs again from the start; its progress must already
 * be saved in its arguments or in the object it works on. A kernel task stops by letting the
 * interrupt in and yielding. With nothing waiting, the loop carries on.
 */

#ifndef KERNEL_CHECKPOINT_CHECKPOINT_H
#define KERNEL_CHECKPOINT_CHECKPOINT_H

#include "kernel/sched/sched.h"
#include <arch/cpu.h>
#include <arch/regs.h>
#include <stdbool.h>

/** @brief Pages a checkpointed loop handles between two checks. */
#define CHECKPOINT_PAGES 4

/**
 * @brief Check whether the current work should stop at this checkpoint.
 *
 * @return true if an interrupt is pending or a reschedule was requested.
 */
static inline bool CheckpointDue(void) { return do_resched || ArchIrqPending(); }

/**
 * @brief Syscall checkpoint: if one is due, rewind the syscall and return from the handler.
 *
 * Use only where the frame's arguments already describe the remaining work.
 *
 * @param frame The caller's trap frame.
 */
#define CHECKPOINT_SVC(frame)                                                                      \
    do {                                                                                           \
        if (CheckpointDue()) {                                                                     \
            ArchRewindSvc(frame);                                                                  \
            return;                                                                                \
        }                                                                                          \
    } while (0)

/**
 * @brief Kernel task checkpoint: take a pending interrupt, then yield if a reschedule is due.
 */
static inline void CheckpointTask(void)
{
    if (ArchIrqPending())
        SchedOpenIrqWindow();
    if (do_resched) {
        do_resched = 0;
        Schedule();
    }
}

#endif /* KERNEL_CHECKPOINT_CHECKPOINT_H */

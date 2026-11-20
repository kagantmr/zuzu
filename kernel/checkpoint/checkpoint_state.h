/**
 * @file checkpoint_state.h
 *
 * @brief Progress a task keeps across a checkpointed syscall that its arguments cannot describe.
 */

#ifndef KERNEL_CHECKPOINT_CHECKPOINT_STATE_H
#define KERNEL_CHECKPOINT_CHECKPOINT_STATE_H

#include <stddef.h>
#include <stdint.h>

/** @brief Which syscall left the checkpoint state behind. */
typedef enum {
    CHECKPOINT_NONE,       /**< No syscall is in progress. */
    CHECKPOINT_CREATE_MEM, /**< CreateMem(MEM_CONTIG) zeroing its pages. */
} CheckpointKind;

/** @brief A half-done syscall's progress, kept in its task until the syscall runs again. */
typedef struct {
    CheckpointKind kind;     /**< The syscall in progress, or CHECKPOINT_NONE. */
    void *obj;               /**< The object being built. */
    size_t pos;              /**< How far the syscall got. */
    void (*drop)(void *obj); /**< Releases @c obj if the task dies before the syscall finishes. */
} CheckpointState;

#endif /* KERNEL_CHECKPOINT_CHECKPOINT_STATE_H */

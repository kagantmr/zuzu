/**
 * @file lbx.h
 *
 * @brief The litterbox task `lbx` is a kernel task in charge of cleaning up large objects.
 */

#ifndef KERNEL_LBX_LBX_H
#define KERNEL_LBX_LBX_H

#include "lbx_node.h"

/**
 * @brief Creates the litterbox task.
 */
void LbxInit(void);

/**
 * @brief Queues a dead object for reclamation one priority level above the current task.
 *
 * Does nothing if the node is already queued.
 *
 * @param[in,out] n The object's node.
 * @param[in] step Reclaims the next part of the object.
 */
void LbxQueue(LbxNode *n, LbxStepFn step);

/**
 * @brief Queues work that only runs when nothing above priority 0 is ready.
 *
 * Does nothing if the node is already queued.
 *
 * @param[in,out] n The work's node.
 * @param[in] step Does the next part of the work.
 */
void LbxQueueIdle(LbxNode *n, LbxStepFn step);

/**
 * @brief Takes an object off the queue, if it is queued.
 *
 * @param[in,out] n The object's node.
 */
void LbxCancel(LbxNode *n);

#endif /* KERNEL_LBX_LBX_H */

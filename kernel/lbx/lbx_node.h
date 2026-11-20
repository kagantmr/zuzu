/**
 * @file lbx_node.h
 *
 * @brief The link an object embeds to be queued on the litterbox task.
 */

#ifndef KERNEL_LBX_LBX_NODE_H
#define KERNEL_LBX_LBX_NODE_H

#include <list.h>
#include <types.h>

/** @brief What a step left to do. */
typedef enum {
    LBX_DONE, /**< The object is fully reclaimed; its node must not be touched again. */
    LBX_MORE  /**< Work is left; queue the object again. */
} LbxResult;

/** @brief A queued object; see struct LbxNode. */
typedef struct LbxNode LbxNode;

/**
 * @brief Reclaims part of a dead object.
 *
 * @param[in,out] n The object's node.
 * @param[in] budget The most work units to do.
 * @return LBX_MORE if work is left, LBX_DONE once the object is gone.
 */
typedef LbxResult (*LbxStepFn)(LbxNode *n, uint32_t budget);

/** @brief Litterbox queue link, embedded in every object lbx can reclaim. */
struct LbxNode {
    ListNode node;  /**< Link in the queue of its priority level; NULL while not queued. */
    Prio prio;      /**< The priority of the task that queued it. */
    LbxStepFn step; /**< Reclaims the next part of the object. */
};

#endif /* KERNEL_LBX_LBX_NODE_H */

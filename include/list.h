/**
 * @file
 * @brief Intrusive circular doubly linked list with a sentinel head.
 */

#ifndef LIB_LIST_H
#define LIB_LIST_H

#ifdef __cplusplus
extern "C" {
#endif

#include <compiler.h>
#include <stddef.h>

/** @brief A link, embedded in the listed object. */
typedef struct ListNodeStruct {
    struct ListNodeStruct *prev; /**< Previous node. */
    struct ListNodeStruct *next; /**< Next node. */
} ListNode;

/** @brief A list: the sentinel node, which points to itself when the list is empty. */
typedef struct {
    ListNode node; /**< The sentinel. */
} ListHead;

/** @brief Walks a list with @p n holding the next node, so @p pos may be removed. */
#define LIST_FOR_EACH_SAFE(pos, n, head)                                                           \
    for ((pos) = (head)->next, (n) = (pos)->next; (pos) != (head); (pos) = (n), (n) = (pos)->next)

/** @brief Walks a list from @p head's sentinel; @p pos must not be removed. */
#define LIST_FOR_EACH(pos, head) for ((pos) = (head)->next; (pos) != (head); (pos) = (pos)->next)

/** @brief Static initializer for an empty ListHead named @p name. */
#define LIST_HEAD_INIT(name)                                                                      \
    {                                                                                              \
        {                                                                                          \
            &(name).node, &(name).node                                                             \
        }                                                                                          \
    }

/**
 * @brief Adds a new node to the start of the list.
 *
 * @param[in,out] node Pointer to the new node to be added.
 * @param[in,out] head Pointer to the head of the list.
 */
static __always_inline void ListAddHead(ListNode *node, ListNode *head)
{
    ListNode *first = head->next;
    node->next = first;
    node->prev = head;
    first->prev = node;
    head->next = node;
}
    
/**
 * @brief Adds a new node to the end of the list.
 *
 * On the IPC hot path this runs on every Call/Reply/WaitOn
 * block-and-enqueue -- a true leaf (no loop, no calls), so always_inline
 * turns it back into straight-line pointer stores instead of a call/ret
 * across TUs.
 *
 * @param[in,out] node Pointer to the new node to be added.
 * @param[in,out] head Pointer to the head of the list.
 */
static __always_inline void ListAddTail(ListNode *node, ListNode *head)
{
    ListNode *tail = head->prev;
    tail->next = node;
    node->prev = tail;
    node->next = head;
    head->prev = node;
}

/**
 * @brief Removes a node from the list.
 *
 * Same leaf shape as ListAddTail(): called from ListPopFront() on every
 * IPC dequeue and from every timeout-cancel path. The node's links are set to NULL.
 *
 * @param[in,out] node Pointer to the node to be removed.
 */
static __always_inline void ListRemove(ListNode *node)
{
    ListNode *prev = node->prev;
    ListNode *next = node->next;
    prev->next = next;
    next->prev = prev;
    node->next = node->prev = NULL;
}

/** @brief Gets the object of type @p type that embeds @p ptr as its field @p member. */
#define container_of(ptr, type, member) ((type *)(void *)((char *)(ptr) - offsetof(type, member)))

/**
 * @brief Initializes a list head.
 *
 * @param[out] head Pointer to the list head to be initialized.
 */
static inline void ListInit(ListHead *head)
{
    head->node.next = &head->node;
    head->node.prev = &head->node;
}

/**
 * @brief Checks if the list is empty.
 *
 * @param[in] head Pointer to the list head.
 * @return 1 if the list is empty, 0 otherwise.
 */
static inline int ListIsEmpty(const ListHead *head) { return head->node.next == &head->node; }

/**
 * @brief Pops the first node from the list and returns it.
 *
 * @param[in,out] head Pointer to the list head.
 * @return Pointer to the popped node, or NULL if the list is empty.
 */
static inline ListNode *ListPopFront(ListHead *head)
{
    if (ListIsEmpty(head)) {
        return NULL;
    }
    ListNode *first = head->node.next;
    ListRemove(first);
    return first;
}

#ifdef __cplusplus
}
#endif

#endif // LIB_LIST_H
#ifndef LIB_LIST_H
#define LIB_LIST_H

#ifdef __cplusplus
extern "C" {
#endif

#include <compiler.h>
#include <stddef.h>

typedef struct ListNodeStruct {
    struct ListNodeStruct *prev, *next;
} ListNode;

typedef struct {
    ListNode node; // sentinel node (empty list points to itself)
} ListHead;

#define LIST_FOR_EACH_SAFE(pos, n, head)                                                           \
    for ((pos) = (head)->next, (n) = (pos)->next; (pos) != (head); (pos) = (n), (n) = (pos)->next)

#define LIST_FOR_EACH(pos, head) for ((pos) = (head)->next; (pos) != (head); (pos) = (pos)->next)

#define LIST_HEAD_INIT(name)                                                                      \
    {                                                                                              \
        {                                                                                          \
            &(name).node, &(name).node                                                             \
        }                                                                                          \
    }

/**
    * @brief Adds a new node to the start of the list.
    *
    *
    * @param node Pointer to the new node to be added.
    * @param head Pointer to the head of the list.
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
 * @param node Pointer to the new node to be added.
 * @param head Pointer to the head of the list.
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
 * Same leaf shape as list_add_tail: called from list_pop_front on every
 * IPC dequeue and from every timeout-cancel path.
 *
 * @param node Pointer to the node to be removed.
 */
static __always_inline void ListRemove(ListNode *node)
{
    ListNode *prev = node->prev;
    ListNode *next = node->next;
    prev->next = next;
    next->prev = prev;
    node->next = node->prev = NULL;
}

#define container_of(ptr, type, member) ((type *)(void *)((char *)(ptr) - offsetof(type, member)))

/**
 * @brief Initializes a list head.
 *
 * @param head Pointer to the list head to be initialized.
 */
static inline void ListInit(ListHead *head)
{
    head->node.next = &head->node;
    head->node.prev = &head->node;
}

/**
 * @brief Checks if the list is empty.
 *
 * @param head Pointer to the list head.
 * @return int Returns 1 if the list is empty, 0 otherwise.
 */
static inline int ListIsEmpty(const ListHead *head) { return head->node.next == &head->node; }

/**
 * @brief Pops the first node from the list and returns it.
 *
 * @param head Pointer to the list head.
 * @return ListNode * Pointer to the popped node, or NULL if the list is empty.
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
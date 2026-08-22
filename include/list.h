#ifndef LIB_LIST_H
#define LIB_LIST_H

#ifdef __cplusplus
extern "C" {
#endif

#include <compiler.h>
#include <stddef.h>

typedef struct list_node {
    struct list_node *prev, *next;
} ListNode;

typedef struct list_head {
    ListNode node;  // sentinel node (empty list points to itself)
} ListHead;

#define list_for_each(pos, head) \
    for (pos = (head)->next; pos != (head); pos = pos->next)

#define list_for_each_safe(pos, n, head) \
    for (pos = (head)->next, n = pos->next; pos != (head); pos = n, n = pos->next)

#define LIST_HEAD_INIT(name) { { &(name).node, &(name).node } }

/**
 * @brief Adds a new node to the end of the list.
 *
 * On the IPC hot path this runs on every SysMsgSend/Recv/Call
 * block-and-enqueue and every waitany registration -- a true leaf (no
 * loop, no calls), so always_inline turns it back into straight-line
 * pointer stores instead of a call/ret across TUs.
 *
 * @param node Pointer to the new node to be added.
 * @param head Pointer to the head of the list.
 */
static __always_inline void list_add_tail(ListNode* node, ListNode* head) {
    ListNode* tail = head->prev;
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
static __always_inline void list_remove(ListNode* node) {
    ListNode* prev = node->prev;
    ListNode* next = node->next;
    prev->next = next;
    next->prev = prev;
    node->next = node->prev = NULL;
}

#define container_of(ptr, type, member) \
    ((type*)((char*)(ptr) - offsetof(type, member)))

/**
 * @brief Initializes a list head.
 * 
 * @param head Pointer to the list head to be initialized.
 */
static inline void list_init(ListHead *head) {
    head->node.next = &head->node;
    head->node.prev = &head->node;
}

/**
 * @brief Checks if the list is empty.
 * 
 * @param head Pointer to the list head.
 * @return int Returns 1 if the list is empty, 0 otherwise.
 */
static inline int list_empty(const ListHead *head) {
    return head->node.next == &head->node;
}

/**
 * @brief Inserts a new node before an existing node in the list.
 * 
 * @param new Pointer to the new node to be inserted.
 * @param existing Pointer to the existing node before which the new node will be inserted.
 */
static inline void list_insert_before(ListNode *new, ListNode *existing) {
    new->next = existing;
    new->prev = existing->prev;
    existing->prev->next = new;
    existing->prev = new;
}

/**
 * @brief Pops the first node from the list and returns it.
 * 
 * @param head Pointer to the list head.
 * @return list_node_t* Pointer to the popped node, or NULL if the list is empty.
 */
static inline ListNode* list_pop_front(ListHead *head) {
    if (list_empty(head)) {
        return NULL;
    }
    ListNode *first = head->node.next;
    list_remove(first);
    return first;
}

#ifdef __cplusplus
}
#endif

#endif // LIB_LIST_H
#ifndef LIB_LIST_H
#define LIB_LIST_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

typedef struct list_node {
    struct list_node *prev, *next;
} list_node_t;

typedef struct list_head {
    list_node_t node;  // sentinel node (empty list points to itself)
} list_head_t;

#define list_for_each(pos, head) \
    for (pos = (head)->next; pos != (head); pos = pos->next)

#define LIST_HEAD_INIT(name) { { &(name).node, &(name).node } }

/**
 * @brief Adds a new node to the end of the list.
 * 
 * @param node Pointer to the new node to be added.
 * @param head Pointer to the head of the list.
 */
void list_add_tail(list_node_t* node, list_node_t* head);

/**
 * @brief Removes a node from the list.
 * 
 * @param node Pointer to the node to be removed.
 */
void list_remove(list_node_t* node);

#define container_of(ptr, type, member) \
    ((type*)((char*)(ptr) - offsetof(type, member)))

/**
 * @brief Initializes a list head.
 * 
 * @param head Pointer to the list head to be initialized.
 */
static inline void list_init(list_head_t *head) {
    head->node.next = &head->node;
    head->node.prev = &head->node;
}

/**
 * @brief Checks if the list is empty.
 * 
 * @param head Pointer to the list head.
 * @return int Returns 1 if the list is empty, 0 otherwise.
 */
static inline int list_empty(const list_head_t *head) {
    return head->node.next == &head->node;
}

/**
 * @brief Inserts a new node before an existing node in the list.
 * 
 * @param new Pointer to the new node to be inserted.
 * @param existing Pointer to the existing node before which the new node will be inserted.
 */
static inline void list_insert_before(list_node_t *new, list_node_t *existing) {
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
static inline list_node_t* list_pop_front(list_head_t *head) {
    if (list_empty(head)) {
        return NULL;
    }
    list_node_t *first = head->node.next;
    list_remove(first);
    return first;
}

#ifdef __cplusplus
}
#endif

#endif // LIB_LIST_H
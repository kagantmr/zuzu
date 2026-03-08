#ifndef LIB_LIST_H
#define LIB_LIST_H

#include <stddef.h>

typedef struct list_node {
    struct list_node *prev, *next;
} list_node_t;

typedef struct list_head {
    list_node_t node;  // sentinel node (empty list points to itself)
} list_head_t;

#define LIST_HEAD_INIT(name) { { &(name).node, &(name).node } }

void list_add_tail(list_node_t* node, list_node_t* head);
void list_remove(list_node_t* node);

#define container_of(ptr, type, member) \
    ((type*)((char*)(ptr) - offsetof(type, member)))

static inline void list_init(list_head_t *head) {
    head->node.next = &head->node;
    head->node.prev = &head->node;
}

static inline int list_empty(const list_head_t *head) {
    return head->node.next == &head->node;
}

static inline list_node_t* list_pop_front(list_head_t *head) {
    if (list_empty(head)) {
        return NULL;
    }
    list_node_t *first = head->node.next;
    list_remove(first);
    return first;
}

#endif // LIB_LIST_H
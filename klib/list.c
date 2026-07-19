#include "list.h"

void list_add_tail(list_node_t* node, list_node_t* head) {
    list_node_t* tail = head->prev;
    tail->next = node;
    node->prev = tail;
    node->next = head;
    head->prev = node;
}

void list_remove(list_node_t* node) {
    list_node_t* prev = node->prev;
    list_node_t* next = node->next;
    prev->next = next;
    next->prev = prev;
    node->next = node->prev = NULL;
}


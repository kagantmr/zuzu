#include "list.h"

void list_add_tail(ListNode* node, ListNode* head) {
    ListNode* tail = head->prev;
    tail->next = node;
    node->prev = tail;
    node->next = head;
    head->prev = node;
}

void list_remove(ListNode* node) {
    ListNode* prev = node->prev;
    ListNode* next = node->next;
    prev->next = next;
    next->prev = prev;
    node->next = node->prev = NULL;
}


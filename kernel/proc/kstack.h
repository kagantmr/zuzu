#ifndef KERNEL_STACK_H
#define KERNEL_STACK_H

#include <stdint.h>
#include <zuzu/types.h>
#include BOARD_LAYOUT_H
#define KSTACK_SLOT_SIZE  0x2000
#define KSTACK_GUARD_SIZE 0x1000
#define KSTACK_REGION_TOP (KSTACK_REGION_BASE + (64u * 0x2000u))

static inline int kstack_slot_from_top(vaddr_t stack_top) {
    return (int)((stack_top - KSTACK_REGION_BASE) / KSTACK_SLOT_SIZE) - 1;
}

static inline vaddr_t kstack_top_from_slot(int slot) {
    return KSTACK_REGION_BASE + (slot + 1) * KSTACK_SLOT_SIZE;
}

vaddr_t kstack_alloc(void);
void kstack_free(vaddr_t stack_top);


#endif
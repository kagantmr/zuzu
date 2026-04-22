#ifndef KERNEL_STACK_H
#define KERNEL_STACK_H

#include <stdint.h>

#define USR_SP 0x7FFFE000
#define KSTACK_REGION_BASE 0xF1000000
#define KSTACK_SLOT_SIZE  0x2000
#define KSTACK_GUARD_SIZE 0x1000

static inline int kstack_slot_from_top(uintptr_t stack_top) {
    return (int)((stack_top - KSTACK_REGION_BASE) / KSTACK_SLOT_SIZE) - 1;
}

static inline uintptr_t kstack_top_from_slot(int slot) {
    return KSTACK_REGION_BASE + (slot + 1) * KSTACK_SLOT_SIZE;
}

uintptr_t kstack_alloc(void);
void kstack_free(uintptr_t stack_top);


#endif
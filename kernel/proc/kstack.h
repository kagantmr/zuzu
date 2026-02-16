#ifndef KERNEL_STACK_H
#define KERNEL_STACK_H

#include "stdint.h"

#define USR_SP 0x7FFFF000
#define KSTACK_REGION_BASE 0xF1000000

uintptr_t kstack_alloc(void);
void kstack_free(uintptr_t stack_top);


#endif
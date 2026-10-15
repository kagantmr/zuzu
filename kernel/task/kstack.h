#ifndef KERNEL_TASK_KSTACK_H
#define KERNEL_TASK_KSTACK_H

#include <types.h>
#include BOARD_LAYOUT_H

#define MAX_KSTACKS 256
#define KSTACK_SLOT_SIZE 0x2000
#define KSTACK_GUARD_SIZE 0x1000

#define KSTACK_REGION_TOP (KSTACK_REGION_BASE + (MAX_KSTACKS * KSTACK_SLOT_SIZE))

_Static_assert(KSTACK_REGION_TOP <= IOREMAP_END, "kstack region overflows the ioremap window");

static inline int KStackSlotFromTop(VirtAddr stack_top)
{
    return (int)((stack_top - KSTACK_REGION_BASE) / KSTACK_SLOT_SIZE) - 1;
}

static inline VirtAddr KStackTopFromSlot(int slot)
{
    return KSTACK_REGION_BASE + (VirtAddr)((slot + 1) * KSTACK_SLOT_SIZE);
}

VirtAddr KStackAlloc(void);
void KStackFree(VirtAddr stack_top);

#endif

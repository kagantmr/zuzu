#ifndef KERNEL_STACK_H
#define KERNEL_STACK_H

#include <stdint.h>
#include <zuzu/types.h>
#include BOARD_LAYOUT_H

#define MAX_KSTACKS 256 /* system-wide kernel stack pool (each is a resident physical stack) */
#define KSTACK_SLOT_SIZE 0x2000 /* 8KB per slot: 4KB guard (unmapped) + 4KB usable page */
#define KSTACK_GUARD_SIZE 0x1000

/* VA window scales with the pool size, not a hardcoded slot count. */
#define KSTACK_REGION_TOP (KSTACK_REGION_BASE + (MAX_KSTACKS * KSTACK_SLOT_SIZE))

/* Bitmap is uint64_t words, so the pool must be a multiple of 64. */
_Static_assert(MAX_KSTACKS % 64 == 0,
	       "MAX_KSTACKS must be a multiple of 64 (uint64_t bitmap words)");
/* The kstack VA window must fit under IOREMAP_END. */
_Static_assert(KSTACK_REGION_TOP <= IOREMAP_END, "kstack region overflows the ioremap window");

static inline int KernelStackSlotFromTop(VirtAddr stack_top)
{
	return (int)((stack_top - KSTACK_REGION_BASE) / KSTACK_SLOT_SIZE) - 1;
}

static inline VirtAddr KernelStackTopFromSlot(int slot)
{
	return KSTACK_REGION_BASE + (slot + 1) * KSTACK_SLOT_SIZE;
}

VirtAddr KernelStackAlloc(void);
void KernelStackFree(VirtAddr stack_top);

#endif

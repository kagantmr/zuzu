/**
 * @file kstack.h
 * @brief Kernel stacks: one per task, carved out of a fixed window of kernel address space.
 *
 * Each stack gets a slot of KSTACK_SLOT_SIZE bytes. The lower KSTACK_GUARD_SIZE bytes of the slot
 * are left unmapped so an overflow faults, and the rest is backed by a single page.
 */

#ifndef KERNEL_TASK_KSTACK_H
#define KERNEL_TASK_KSTACK_H

#include <types.h>
#include BOARD_LAYOUT_H

/** @brief Number of kernel stack slots, which is the most tasks that can exist. */
#define MAX_KSTACKS 256
/** @brief Address space reserved for one stack, guard included. */
#define KSTACK_SLOT_SIZE 0x2000
/** @brief Unmapped bytes at the bottom of each slot. */
#define KSTACK_GUARD_SIZE 0x1000

/** @brief End of the kernel stack window. */
#define KSTACK_REGION_TOP (KSTACK_REGION_BASE + (MAX_KSTACKS * KSTACK_SLOT_SIZE))

_Static_assert(KSTACK_REGION_TOP <= IOREMAP_END, "kstack region overflows the ioremap window");

/**
 * @brief Find the slot a stack belongs to.
 *
 * @param[in] stack_top The top address KStackAlloc() returned.
 * @return The slot index.
 */
static inline int KStackSlotFromTop(VirtAddr stack_top)
{
    return (int)((stack_top - KSTACK_REGION_BASE) / KSTACK_SLOT_SIZE) - 1;
}

/**
 * @brief Get the top address of a slot's stack.
 *
 * @param[in] slot The slot index.
 * @return The address the stack pointer starts from.
 */
static inline VirtAddr KStackTopFromSlot(int slot)
{
    return KSTACK_REGION_BASE + (VirtAddr)((slot + 1) * KSTACK_SLOT_SIZE);
}

/**
 * @brief Take a free slot and map a page for its stack.
 *
 * @return The top of the new stack, or 0 if every slot is in use or no frame could be allocated.
 */
VirtAddr KStackAlloc(void);
/**
 * @brief Unmap a stack, free its page, and return its slot.
 *
 * @param[in] stack_top The top address KStackAlloc() returned.
 */
void KStackFree(VirtAddr stack_top);

#endif

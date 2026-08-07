#include "kstack.h"
#include "kernel/mm/pmm.h"
#include "kernel/mm/vmm.h"
#include "stdbool.h"
#include <arch/mmu.h>
#include <assert.h>
#include <zuzu/types.h>

#define KSTACK_WORDS (MAX_KSTACKS / 64)

/* Bitmap: bit N set = kstack slot N in use. Word-indexed for >64 slots. */
static uint64_t bitmap[KSTACK_WORDS];
static PhysAddr slot_pa[MAX_KSTACKS];

VirtAddr KernelStackAlloc(void)
{
	/* Scan word by word for a free bit, exactly like TcbSlotAlloc. */
	for (uint32_t w = 0; w < KSTACK_WORDS; w++) {
		uint64_t free = ~bitmap[w];
		if (!free)
			continue; /* this word full, next */
		uint32_t bit = __builtin_ctzll(free);
		uint32_t slot = w * 64 + bit;
		if (slot >= MAX_KSTACKS)
			return 0; /* free bit was in the unused tail */

		PhysAddr page_pa = PmmAllocFrame();
		if (!page_pa)
			return 0;
		slot_pa[slot] = page_pa;

		VirtAddr slot_va = KernelStackTopFromSlot((int)slot) - KSTACK_SLOT_SIZE;

		/* Map the usable stack page (above the guard). */
		bool result = VmmMapRange(VmmGetKernelAddrspace(), slot_va + KSTACK_GUARD_SIZE,
					    page_pa, PAGE_SIZE, PROT_READ | PROT_WRITE,
					    VM_MEM_NORMAL, VM_OWNER_ANON, VM_FLAG_NONE);
		if (!result) {
			PmmFreeFrame(page_pa);
			slot_pa[slot] = 0;
			return 0;
		}

		/* Unmap the guard page (may have been part of a section mapping). */
		if (!arch_mmu_unmap_page(VmmGetKernelAddrspace(), slot_va)) {
			/* If translation is already absent, the guard page is already in
			 * the desired state and this is not an allocation failure. */
			if (arch_mmu_translate(VmmGetKernelAddrspace()->pt_root_physaddr, slot_va) != 0) {
				VmmUnmapRange(VmmGetKernelAddrspace(), slot_va + KSTACK_GUARD_SIZE,
						PAGE_SIZE);
				PmmFreeFrame(page_pa);
				slot_pa[slot] = 0;
				return 0;
			}
		}
		arch_mmu_flush_tlb_va(slot_va);
		arch_mmu_barrier();

		bitmap[w] |= (1ULL << bit);
		return KernelStackTopFromSlot((int)slot);
	}
	return 0; /* pool exhausted */
}

void KernelStackFree(VirtAddr stack_top)
{
	int slot = KernelStackSlotFromTop(stack_top);
	VirtAddr mapped_va = KernelStackTopFromSlot(slot) - KSTACK_SLOT_SIZE + KSTACK_GUARD_SIZE;
	VmmUnmapRange(VmmGetKernelAddrspace(), mapped_va, PAGE_SIZE);
	PmmFreeFrame(slot_pa[slot]);
	slot_pa[slot] = 0;
	bitmap[slot / 64] &= ~(1ULL << (slot % 64));
}

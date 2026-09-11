#include "kstack.h"
#include "kernel/mm/pmm.h"
#include "kernel/mm/vmm.h"
#include "stdbool.h"
#include <arch/mmu.h>
#include <arch/barrier.h>
#include <assert.h>
#include <bitmap.h>
#include <zuzu/types.h>

/* Bit N set = kstack slot N in use. */
static uint32_t bitmap[BITMAP_WORDS(MAX_KSTACKS)];
static PhysAddr slot_pa[MAX_KSTACKS];

VirtAddr KernelStackAlloc(void)
{
	int found = BitmapFindFirstZero(bitmap, MAX_KSTACKS);
	if (found >= 0) {
		uint32_t slot = (uint32_t)found;

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
						PAGE_SIZE, true);
				PmmFreeFrame(page_pa);
				slot_pa[slot] = 0;
				return 0;
			}
		}
		arch_mmu_flush_tlb_va(slot_va);
		ArchCtxSync();

		BitmapSet(bitmap, slot);
		return KernelStackTopFromSlot((int)slot);
	}
	return 0; /* pool exhausted */
}

void KernelStackFree(VirtAddr stack_top)
{
	int slot = KernelStackSlotFromTop(stack_top);
	VirtAddr mapped_va = KernelStackTopFromSlot(slot) - KSTACK_SLOT_SIZE + KSTACK_GUARD_SIZE;
	VmmUnmapRange(VmmGetKernelAddrspace(), mapped_va, PAGE_SIZE, true);
	PmmFreeFrame(slot_pa[slot]);
	slot_pa[slot] = 0;
	BitmapClr(bitmap, (size_t)slot);
}

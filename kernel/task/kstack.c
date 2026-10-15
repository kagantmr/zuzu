#include "kstack.h"
#include "kernel/mm/pmm/pmm.h"
#include "kernel/mm/vmm/vmm.h"
#include "stdbool.h"
#include <arch/barrier.h>
#include <arch/mmu.h>
#include <assert.h>
#include <bitmap.h>
#include <types.h>

static uint32_t bitmap[BITMAP_WORDS(MAX_KSTACKS)];
static PhysAddr slot_pa[MAX_KSTACKS];

VirtAddr KStackAlloc(void)
{
    int found = BitmapFindFirstZero(bitmap, MAX_KSTACKS);
    if (found >= 0) {
        uint32_t slot = (uint32_t)found;

        PhysAddr page_pa = PmmAllocFrame();
        if (!page_pa)
            return 0;
        slot_pa[slot] = page_pa;

        VirtAddr slot_va = KStackTopFromSlot((int)slot) - KSTACK_SLOT_SIZE;

        bool result = VmmMapRange(VmmGetKernelAddressSpace(), slot_va + KSTACK_GUARD_SIZE, page_pa,
                                  PAGE_SIZE, PROT_READ | PROT_WRITE, VM_MEM_NORMAL);
        if (!result) {
            PmmFreeFrame(page_pa);
            slot_pa[slot] = 0;
            return 0;
        }

        if (!ArchMmuUnmapPage(VmmGetKernelAddressSpace(), slot_va)) {
            
            if (ArchMmuTranslate(VmmGetKernelAddressSpace()->pt_root_physaddr, slot_va) != 0) {
                VmmUnmapRange(VmmGetKernelAddressSpace(), slot_va + KSTACK_GUARD_SIZE, PAGE_SIZE,
                              true);
                PmmFreeFrame(page_pa);
                slot_pa[slot] = 0;
                return 0;
            }
        }
        ArchMmuFlushTlbVa(slot_va);
        ArchSyncBarrier();

        BitmapSet(bitmap, slot);
        return KStackTopFromSlot((int)slot);
    }
    return 0;
}

void KStackFree(VirtAddr stack_top)
{
    int slot = KStackSlotFromTop(stack_top);
    VirtAddr mapped_va = KStackTopFromSlot(slot) - KSTACK_SLOT_SIZE + KSTACK_GUARD_SIZE;
    VmmUnmapRange(VmmGetKernelAddressSpace(), mapped_va, PAGE_SIZE, true);
    PmmFreeFrame(slot_pa[slot]);
    slot_pa[slot] = 0;
    BitmapClr(bitmap, (size_t)slot);
}

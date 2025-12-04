#include "arch/arm/symbols.h"
#include "kernel/include/layout.h"
#include "arch/arm/mmu/phys.h"
#include "kernel/mm/pmm.h"
#include "kernel/mm/reserve.h"
#include "core/log.h"
#include "core/panic.h"
#include "kernel/kmain.h"
#include "lib/mem.h"

extern kernel_layout_t kernel_layout;
extern pmm_state_t pmm_state;
extern phys_region_t phys_region[];

void kernel_early(void) {
    /* Fill kernel layout */
    kernel_layout.kernel_start = (uintptr_t)_kernel_start;
    kernel_layout.kernel_end   = (uintptr_t)_kernel_end;
    kernel_layout.stack_top    = (uintptr_t)__stack_top__;
    kernel_layout.stack_base   = (uintptr_t)__stack_base__;

    /* Fill PMM */
    pmm_state.pfn_base   = phys_region->start / PAGE_SIZE;
    pmm_state.pfn_end    = phys_region->end / PAGE_SIZE;
    pmm_state.total_pages = pmm_state.pfn_end - pmm_state.pfn_base;
    pmm_state.free_pages  = pmm_state.total_pages;

    /* Place bitmap just after kernel, page aligned */
    uintptr_t bitmap_phys_start = align_up(kernel_layout.kernel_end, PAGE_SIZE);

    /* compute bitmap size in bytes and round up to full pages */
    size_t bitmap_bytes = (pmm_state.total_pages + 7) / 8;
    size_t bitmap_page_bytes = align_up(bitmap_bytes, PAGE_SIZE);
    uintptr_t bitmap_phys_end = bitmap_phys_start + bitmap_page_bytes;

    /* sanity: ensure bitmap fits below stack */
    if (bitmap_phys_end > kernel_layout.stack_top) {
        panic("Bitmap does not fit in RAM before stack!");
    }

    /* Install bitmap */
    pmm_state.bitmap = (uint8_t*)bitmap_phys_start;
    pmm_state.bitmap_bytes = bitmap_bytes;

    /* zero bitmap pages */
    memset((void*)bitmap_phys_start, 0, bitmap_page_bytes);

    /* record in layout */
    kernel_layout.bitmap_start = bitmap_phys_start;
    kernel_layout.bitmap_end   = bitmap_phys_end;

    /* reserve kernel, stack, and bitmap pages */

    mark(kernel_layout.kernel_start, kernel_layout.kernel_end);
    mark(kernel_layout.bitmap_start, kernel_layout.bitmap_end);
    

    // Switch to larger stack, first allocate pages for the stack
    size_t stack_pages = 2; // 8 KB 
    uintptr_t kernel_stack = 0;
    for (size_t i = 0; i < stack_pages; i++) {
        uintptr_t page = alloc_page();
        if (!page) panic("Out of memory for kernel stack");
        kernel_stack = page; // pick the last allocated page as the top
    }

    // Use inline assembly to move to the larger stack
    uintptr_t stack_top = kernel_stack + stack_pages * PAGE_SIZE;
    __asm__ volatile ("mov sp, %0" : : "r"(stack_top));

    kernel_layout.stack_base = kernel_stack;
    kernel_layout.stack_top  = stack_top;
    mark(kernel_layout.stack_base, kernel_layout.stack_top);
    
    // Call main kernel
    kmain();
}
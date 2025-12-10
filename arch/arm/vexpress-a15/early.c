#include "symbols.h"
#include "layout.h"
#include "phys.h"
#include "irq.h"
#include "pmm.h"
#include "reserve.h"
#include "string.h"
#include "log.h"
#include "panic.h"
#include "kmain.h"
#include "mem.h"
#include "uart.h"
#include "dtb_parser.h"

extern kernel_layout_t kernel_layout;
extern pmm_state_t pmm_state;
extern phys_region_t phys_region;
dtb_node_t *root;

void early(void* dtb_ptr) {

    root = dtb_parse(dtb_ptr);

    uintptr_t uart0_base = dtb_get_reg_addr(root, "/smb/motherboard/iofpga/uart@090000");
    //if (!uart0_base) uart0_base = 0x1c090000;
    uart_init(uart0_base + 0x1c000000); // UART0 base address is offset by 0x1c000000
    uart_puts("UART initialized in early boot\n");
    kprintf_init(uart_putc);

    kprintf("%u", dtb_get_reg_addr(root, "smb/motherboard/iofpga@3/uart@090000"));

    uint32_t ram_base = dtb_get_reg_addr(root, "memory");
    uint32_t ram_size = dtb_get_reg_size(root, "memory");
    kprintf("Detected RAM: base=0x%x size=0x%x\n", ram_base, ram_size);

    // compute end
    uint32_t ram_end = ram_base + ram_size;
    kprintf("RAM region: 0x%x - 0x%x\n", ram_base, ram_end);

    /* Fill kernel layout */
    kernel_layout.kernel_start = (uintptr_t)_kernel_start;
    kernel_layout.kernel_end   = (uintptr_t)_kernel_end;
    kernel_layout.stack_top    = (uintptr_t)__stack_top__;
    kernel_layout.stack_base   = (uintptr_t)__stack_base__;

    kprintf("Kernel: 0x%x - 0x%x\n",
            (unsigned int)kernel_layout.kernel_start,
            (unsigned int)kernel_layout.kernel_end);
    kprintf("Initial stack: 0x%x - 0x%x\n",
            (unsigned int)kernel_layout.stack_base,
            (unsigned int)kernel_layout.stack_top);

    /* Fill physical region */
    phys_region.start = (uintptr_t)ram_base;
    phys_region.end   = (uintptr_t)ram_end;
    kprintf("Physical RAM region: 0x%x - 0x%x\n",
            (unsigned int)phys_region.start,
            (unsigned int)phys_region.end);

    /* Fill PMM */
    pmm_state.pfn_base   = phys_region.start / PAGE_SIZE;
    pmm_state.pfn_end    = phys_region.end / PAGE_SIZE;
    pmm_state.total_pages = pmm_state.pfn_end - pmm_state.pfn_base;
    pmm_state.free_pages  = pmm_state.total_pages;
    kprintf("PMM: PFN base=%u, PFN end=%u, total pages=%u\n",
            (unsigned int)pmm_state.pfn_base,
            (unsigned int)pmm_state.pfn_end,
            (unsigned int)pmm_state.total_pages);

    /* Place bitmap just after kernel, page aligned */
    uintptr_t bitmap_phys_start = align_up(kernel_layout.kernel_end, PAGE_SIZE);



    /* compute bitmap size in bytes and round up to full pages */
    size_t bitmap_bytes = (pmm_state.total_pages + 7) / 8;
    size_t bitmap_page_bytes = align_up(bitmap_bytes, PAGE_SIZE);
    uintptr_t bitmap_phys_end = bitmap_phys_start + bitmap_page_bytes;

    kprintf("PMM bitmap requires %u bytes (%u pages)\n",
            (unsigned int)bitmap_bytes,
            (unsigned int)(bitmap_page_bytes / PAGE_SIZE));

    kprintf("Placing PMM bitmap at 0x%x - 0x%x\n",
            (unsigned int)bitmap_phys_start,
            (unsigned int)bitmap_phys_end);

    /* sanity: ensure bitmap fits below stack */
    if (bitmap_phys_end > kernel_layout.stack_top) {
        panic("Bitmap does not fit in RAM before stack");
    }

    kprintf("PMM bitmap: 0x%x - 0x%x (%u bytes, %u pages)\n",
            (unsigned int)bitmap_phys_start,
            (unsigned int)bitmap_phys_end,
            (unsigned int)bitmap_bytes,
            (unsigned int)(bitmap_page_bytes / PAGE_SIZE));

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
    

    enable_interrupts();
    // Call main kernel
    kmain();
}
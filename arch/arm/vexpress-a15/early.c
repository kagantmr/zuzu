#include "arch/arm/include/symbols.h"
#include "arch/arm/include/irq.h"

#include "arch/arm/vexpress-a15/board.h"

#include "kernel/layout.h"
#include "kernel/mm/pmm.h"
#include "kernel/mm/reserve.h"
#include "kernel/kmain.h"
#include "kernel/dtb/dtb_parser.h"
#include "kernel/mm/alloc.h"
#include "kernel/vm/vmm.h"


#include "core/assert.h"
#include "core/log.h"
#include "core/panic.h"

#include "lib/mem.h"
#include "lib/string.h"

#include "drivers/uart/uart.h"

#include "drivers/uart/pl011.h"


extern kernel_layout_t kernel_layout;
extern pmm_state_t pmm_state;
extern phys_region_t phys_region;
dtb_node_t *root;
uint32_t bss_check;

void early(void* dtb_ptr) {
    // early(void*) begins its life with minimal stack

    root = dtb_parse(dtb_ptr);

    uintptr_t smb_base = dtb_get_ranges_parent_addr(root, "/smb", 0x03);
    if (smb_base == 0) {
        smb_base = VEXPRESS_SMB_BASE; // Fallback to board default if DTB omits it
    }

    uintptr_t uart_offset = 0;
    #if defined(VEXPRESS_UART0)
    uart_offset = dtb_get_reg_addr(root, "/smb/motherboard/iofpga/uart@090000");
    #elif defined(VEXPRESS_UART1)
    uart_offset = dtb_get_reg_addr(root, "/smb/motherboard/iofpga/uart@0a0000");
    #elif defined(VEXPRESS_UART2)
    uart_offset = dtb_get_reg_addr(root, "/smb/motherboard/iofpga/uart@0b0000");
    #elif defined(VEXPRESS_UART3)
    uart_offset = dtb_get_reg_addr(root, "/smb/motherboard/iofpga/uart@0c0000");
    #else
    #error "No VEXPRESS_UARTx selected"
    #endif

    kassert(uart_offset != 0);
    uart_set_driver(&pl011_driver, smb_base + uart_offset);
    kprintf_init(uart_putc);

    kassert(bss_check == 0); // Ensure BSS is zeroed

    uint32_t ram_base = dtb_get_reg_addr(root, "memory");
    uint32_t ram_size = dtb_get_reg_size(root, "memory");

    kassert(ram_base != 0 && ram_size != 0);

    uint32_t ram_end = ram_base + ram_size;

    /* Fill kernel layout */
    kernel_layout.kernel_start = (uintptr_t)_kernel_start;
    kernel_layout.kernel_end   = (uintptr_t)_kernel_end;
    kernel_layout.stack_top    = (uintptr_t)__svc_stack_top__;
    kernel_layout.stack_base   = (uintptr_t)__svc_stack_base__;

    /* Fill physical region */
    phys_region.start = (uintptr_t)ram_base;
    phys_region.end   = (uintptr_t)ram_end;

    /* Fill PMM */
    pmm_state.pfn_base   = phys_region.start / PAGE_SIZE;
    pmm_state.pfn_end    = phys_region.end / PAGE_SIZE;
    pmm_state.total_pages = pmm_state.pfn_end - pmm_state.pfn_base;
    pmm_state.free_pages  = pmm_state.total_pages;

    /* Place bitmap just after kernel, page aligned */
    uintptr_t bitmap_phys_start = align_up(kernel_layout.kernel_end, PAGE_SIZE);

    /* compute bitmap size in bytes and round up to full pages */
    size_t bitmap_bytes = (pmm_state.total_pages + 7) / 8;
    size_t bitmap_page_bytes = align_up(bitmap_bytes, PAGE_SIZE);
    uintptr_t bitmap_phys_end = bitmap_phys_start + bitmap_page_bytes;


    kassert(bitmap_phys_end <= kernel_layout.stack_top); // Does bitmap fit below stack?
    kassert(bitmap_phys_end <= phys_region.end);         // Does bitmap fit in RAM?
    


    /* Install bitmap */
    pmm_state.bitmap = (uint8_t*)bitmap_phys_start;
    pmm_state.bitmap_bytes = bitmap_bytes;

    /* zero bitmap pages */
    memset((void*)bitmap_phys_start, 0, bitmap_page_bytes);

    /* record in layout */
    kernel_layout.bitmap_start = bitmap_phys_start;
    kernel_layout.bitmap_end   = bitmap_phys_end;

    /* reserve DTB, kernel, bitmap, and all mode stacks */

    pmm_mark_phys_page(kernel_layout.dtb_start, kernel_layout.kernel_start);
    pmm_mark_phys_page(kernel_layout.kernel_start, kernel_layout.kernel_end);
    pmm_mark_phys_page(kernel_layout.bitmap_start, kernel_layout.bitmap_end);
    pmm_mark_phys_page(kernel_layout.stack_base, kernel_layout.stack_top);       // Bootloader SVC stack
    pmm_mark_phys_page((uintptr_t)__irq_stack_base__, (uintptr_t)__irq_stack_top__);  // IRQ stack
    pmm_mark_phys_page((uintptr_t)__abt_stack_base__, (uintptr_t)__abt_stack_top__);  // Abort stack
    pmm_mark_phys_page((uintptr_t)__und_stack_base__, (uintptr_t)__und_stack_top__);  // Undefined stack
    
    kheap_init();

    // Test panic
    //KPANIC("Zuzu chewed on the wires");

    // Enable virtual memory
    KINFO("Attempting to enable MMU.");   
    vmm_bootstrap();
    KINFO("MMU enabled.");

    enable_interrupts();
    // Call main kernel
    kmain();
}
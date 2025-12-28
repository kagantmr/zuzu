#include "arch/arm/include/symbols.h"
#include "arch/arm/mmu/phys.h"
#include "arch/arm/include/irq.h"

#include "arch/arm/vexpress-a15/board.h"

#include "kernel/layout.h"
#include "kernel/mm/pmm.h"
#include "kernel/mm/reserve.h"
#include "kernel/kmain.h"
#include "kernel/dtb/dtb_parser.h"
#include "kernel/mm/alloc.h"


#include "core/assert.h"
#include "core/log.h"
#include "core/panic.h"

#include "lib/mem.h"
#include "lib/string.h"

#include "drivers/uart.h"



extern kernel_layout_t kernel_layout;
extern pmm_state_t pmm_state;
phys_region_t phys_region;
dtb_node_t *root;
uint32_t bss_check;

void early(void* dtb_ptr) {
    // early(void*) begins its life with minimal stack

    root = dtb_parse(dtb_ptr);

    #ifdef VEXPRESS_UART0
    uintptr_t uart_base = dtb_get_reg_addr(root, "/smb/motherboard/iofpga/uart@090000");
    #endif
    #ifdef VEXPRESS_UART1
    uintptr_t uart_base = dtb_get_reg_addr(root, "/smb/motherboard/iofpga/uart@0a0000");
    #endif
    #ifdef VEXPRESS_UART2
    uintptr_t uart_base = dtb_get_reg_addr(root, "/smb/motherboard/iofpga/uart@0b0000");
    #endif
    #ifdef VEXPRESS_UART3
    uintptr_t uart_base = dtb_get_reg_addr(root, "/smb/motherboard/iofpga/uart@0c0000");
    #endif
    
    uart_init(uart_base + VEXPRESS_SMB_BASE); // UART0 base address is offset by 0x1c000000
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

    mark(kernel_layout.dtb_start, kernel_layout.kernel_start);
    mark(kernel_layout.kernel_start, kernel_layout.kernel_end);
    mark(kernel_layout.bitmap_start, kernel_layout.bitmap_end);
    mark(kernel_layout.stack_base, kernel_layout.stack_top);       // Bootloader SVC stack
    mark((uintptr_t)__irq_stack_base__, (uintptr_t)__irq_stack_top__);  // IRQ stack
    mark((uintptr_t)__abt_stack_base__, (uintptr_t)__abt_stack_top__);  // Abort stack
    mark((uintptr_t)__und_stack_base__, (uintptr_t)__und_stack_top__);  // Undefined stack
    
    kheap_init();

    // Test panic
    //KPANIC("Zuzu chewed on the wires");

    enable_interrupts();
    // Call main kernel
    kmain();
}
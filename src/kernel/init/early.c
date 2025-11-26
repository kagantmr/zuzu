#include "arch/arm/symbols.h"
#include "kernel/include/layout.h"
#include "kernel/mm/phys.h"
#include "kernel/mm/pmm.h"
#include "kernel/mm/reserve.h"
#include "kernel/kmain.h"
#include "lib/mem.h"

extern kernel_layout_t kernel_layout;

void kernel_early(void) {


    
    kernel_layout.kernel_start = (uintptr_t) _kernel_start;
    kernel_layout.kernel_end =   (uintptr_t) _kernel_end;
    kernel_layout.stack_base = align_up((uintptr_t)_kernel_end, 8);
    kernel_layout.stack_top = kernel_layout.stack_base + 0x10000;

    // Switch to the stack now that it's computed
    // __asm__ volatile ("mov sp, %0" : : "r"(kernel_layout.stack_txop));

    // Call kmain once that RAM stuffs are settled
    kmain();
}
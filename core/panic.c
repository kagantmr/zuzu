#include "core/panic.h"
#include "lib/string.h"
#include "core/log.h"
#include "core/kprintf.h"
#include "arch/arm/include/irq.h"
#include "arch/arm/include/symbols.h"
#include "drivers/uart/uart.h"
#include "kernel/layout.h"

#include <stdint.h>


extern kernel_layout_t kernel_layout;

void dump_stack(void) {
    uint32_t *sp;
    int count = 0;
    
    // Get current stack pointer
    __asm__ volatile("mov %0, sp" : "=r"(sp));
    
    // Use current kernel stack bounds from layout if initialized
    // Otherwise fall back to bootloader stack bounds from linker symbols
    uintptr_t stack_base_va = kernel_layout.stack_base_va;
    uintptr_t stack_top_va = kernel_layout.stack_top_va;
    
    kprintf("*** Stack dump ***\n");
    kprintf("kernel_layout raw: base=%p top=%p\n", (void*)stack_base_va, (void*)stack_top_va);
    
    if (stack_base_va == 0 || stack_top_va == 0) {
        // kernel_layout not yet initialized, use bootloader stack
        kprintf("Using bootloader stack bounds\n");
        stack_base_va = (uintptr_t)__svc_stack_base__;
        stack_top_va = (uintptr_t)__svc_stack_top__;
    }
    
    kprintf("Stack: %p - %p\n", (void*)stack_base_va, (void*)stack_top_va);
    kprintf("Current SP: %p\n", sp);
    
    // Get kernel code bounds for filtering return addresses
    uintptr_t code_start = (uintptr_t)_kernel_start;
    uintptr_t code_end = (uintptr_t)_kernel_end;
    
    // Dump stack memory looking for likely return addresses
    // Scan from current SP upward toward stack top
    uint32_t *addr = sp;
    while ((uintptr_t)addr < stack_top_va && count < 64) {
        uint32_t value = *addr;
        
        // Check if this value looks like a code address (within kernel .text section)
        if (value >= code_start && value < code_end) {
            kprintf("[%p]: %x (code)\n", addr, value);
        }
        
        addr++;
        count++;
    }
    
    kprintf("********************\n");
}

_Noreturn void panic(void) {
    // Get caller's return address
    void *caller_ra = __builtin_return_address(0);
    
    arch_global_irq_disable();
    
    uart_puts("\nZuzu has panicked.\n");
    kprintf("Triggered from caller @ %p\n", caller_ra);
    
    dump_stack();

    arch_global_irq_disable();
    __asm__ volatile (
        "1:\n"
        "    wfi\n"
        "    b 1b\n"
    );
    while (1);
}


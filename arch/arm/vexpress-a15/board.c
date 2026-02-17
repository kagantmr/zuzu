#include "kernel/dtb/dtb.h"
#include "kernel/vmm/vmm.h"
#include "drivers/uart/pl011.h"
#include "drivers/uart/uart.h"
#include "arch/arm/include/gicv2.h"
#include "arch/arm/timer/generic_timer.h"
#include "core/panic.h"
#include "core/log.h"
#include "core/kprintf.h" 

// arch/arm/vexpress-a15/devices.c
#include "kernel/dtb/dtb.h"
#include "kernel/vmm/vmm.h"
#include "core/panic.h"
#include "core/log.h"
#include "core/kprintf.h"



void board_init_devices(void) {
    char path[128];
    uint64_t addr, size;


    if (dtb_find_compatible("arm,cortex-a15-gic", path, sizeof(path))) {
        uint64_t gicd, gicc, s_d, s_c;
        dtb_get_reg_phys(path, 0, &gicd, &s_d);
        dtb_get_reg_phys(path, 1, &gicc, &s_c);

        void *gicd_va = ioremap(gicd, s_d);
        void *gicc_va = ioremap(gicc, s_c);
        
        if (!gicd_va || !gicc_va) panic("Failed to ioremap GIC");
        
        gic_init((uintptr_t)gicd_va, (uintptr_t)gicc_va);
    } else {
        panic("GIC not found");
    }

    if (dtb_find_compatible("arm,pl011", path, sizeof(path))) {
        if (dtb_get_reg_phys(path, 0, &addr, &size)) {
            void *uart_va = ioremap((uintptr_t)addr, (size_t)size);
            if (!uart_va) panic("Failed to ioremap UART");
            
            uart_set_driver(&pl011_driver, (uintptr_t)uart_va);
            kprintf_init(uart_putc);
            
            // Enable UART RX interrupts
            pl011_init_irq((uintptr_t)uart_va);
            
            KDEBUG("UART re-mapped to %p", uart_va);
        }
    } else {
        KDEBUG("No UART in DTB, keeping early boot config");
    }
    
#ifndef SP804_TIMER
    KDEBUG("Using ARM generic timer as tick source");
    generic_timer_init();
#else
    bool sp804_found = false;
    if (dtb_find_compatible("arm,sp804", path, sizeof(path))) {
        if (dtb_get_reg_phys(path, 0, &addr, &size)) {
            void *sp804_va = ioremap((uintptr_t)addr, (size_t)size);
            if (!sp804_va) panic("Failed to ioremap SP804");

            // Initialize SP804
            sp804_init((uintptr_t)sp804_va, 10000); 
            sp804_start((uintptr_t)sp804_va);
            
            KINFO("Initialized SP804 timer at %p", sp804_va);
            sp804_found = true;
        }
    }

    if (!sp804_found) {
        KWARN("SP804 requested but not found in DTB. Fallback to Generic Timer.");
        generic_timer_init();
    }
#endif
}
// board.c - Board-specific initialization for Versatile Express A15

#include "kernel/dtb/dtb.h"
#include "kernel/mm/vmm.h"
#include "drivers/uart/pl011.h"
#include "drivers/uart/uart.h"
#include "arch/arm/include/gicv2.h"
#include "arch/arm/timer/generic_timer.h"
#include "core/panic.h"
#include "core/kprintf.h" 


#include "kernel/dtb/dtb.h"
#include "kernel/mm/vmm.h"
#include "core/panic.h"
#include "core/kprintf.h"
#include "kernel/boot_info.h"
#include <string.h>

#define LOG_FMT(fmt) "(board) " fmt
#include "core/log.h"

uint32_t rtc_epoch;

void board_init_devices(void) {
    (void)0;
    uint64_t addr, size;


    {
        uint64_t gicd = 0, s_d = 0, gicc = 0, s_c = 0;
        bool found = false;
        const dtb_dev_t *arr = boot_info_dev_array();
        uint32_t cnt = boot_info_dev_count();
        for (uint32_t i = 0; i < cnt; i++) {
            const dtb_dev_t *d = &arr[i];
            if (strcmp(d->compatible, "arm,cortex-a15-gic") == 0) {
                gicd = d->phys;
                s_d = d->size;
                if (d->nregs >= 2) {
                    gicc = d->phys2;
                    s_c = d->size2;
                }
                found = true;
                break;
            }
        }

        if (!found) panic("GIC not found");

        void *gicd_va = ioremap(gicd, s_d);
        void *gicc_va = ioremap(gicc ? gicc : gicd, s_c ? s_c : s_d);

        KDEBUG("GICv2 (GICD) re-mapped to %p", gicd_va);
        
        if (!gicd_va || !gicc_va) panic("Failed to ioremap GIC");
        
        gic_init((uintptr_t)gicd_va, (uintptr_t)gicc_va);
    }

    {
        bool found = false;
        const dtb_dev_t *arr = boot_info_dev_array();
        uint32_t cnt = boot_info_dev_count();
        for (uint32_t i = 0; i < cnt; i++) {
            const dtb_dev_t *d = &arr[i];
            if (strcmp(d->compatible, "arm,pl011") == 0) {
                addr = d->phys;
                size = d->size;
                found = true;
                break;
            }
        }
        if (found) {
            void *uart_va = ioremap((uintptr_t)addr, (size_t)size);
            if (!uart_va) panic("Failed to ioremap UART");

            uart_set_driver(&pl011_driver, (uintptr_t)uart_va);
            kprintf_init(uart_putc);

            KDEBUG("UART re-mapped to %p", uart_va);
        } else {
            KDEBUG("Oops! No UART in DTB, keeping early boot config");
        }
    }
    
    {
        bool found = false;
        const dtb_dev_t *arr = boot_info_dev_array();
        uint32_t cnt = boot_info_dev_count();
        for (uint32_t i = 0; i < cnt; i++) {
            const dtb_dev_t *d = &arr[i];
            if (strcmp(d->compatible, "arm,pl031") == 0) {
                addr = d->phys;
                size = d->size;
                found = true;
                break;
            }
        }

        if (found) {
            void *rtc_va = ioremap((uintptr_t)addr, (size_t)size);
            if (!rtc_va) return;

            rtc_epoch = *((volatile uint32_t *)rtc_va);
            if (rtc_epoch == 0) {
                KDEBUG("Oops! RTC epoch is 0 (check for anomalies)");
            }

            iounmap(rtc_va);
        } else {
            KDEBUG("Oops! No RTC found in DTB, epoch is 0");
        }
    }

    KDEBUG("Using ARM generic timer as tick source");
    generic_timer_init();
}
#include "core/panic.h"
#include "lib/string.h"
#include "core/log.h"
#include "arch/arm/include/irq.h"
#include "drivers/uart.h"

void panic() {

    disable_interrupts();
    
    uart_puts("\nZuzu has panicked.\n");
    __asm__ volatile (
        "1:\n"
        "    wfi\n"
        "    b 1b\n"
    );
}


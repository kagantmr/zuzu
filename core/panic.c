#include "panic.h"
#include "string.h"
#include "log.h"
#include "irq.h"
#include "uart.h"

void panic(const char* message) {
    disable_interrupts();
    
    uart_puts(message);
    uart_puts("\n!!!! [PANIC] !!!!\n");
    __asm__ volatile (
        "1:\n"
        "    wfi\n"
        "    b 1b\n"
    );
}

// Static panic buffer used by panicf()
static char   panicbuf[256];
static size_t panic_i = 0;

// vstrfmt will call this for each output character
static void panicbuf_putc(char c) {
    if (panic_i < sizeof(panicbuf) - 1) {
        panicbuf[panic_i++] = c;
    }
}

/**
 * @brief Handle a kernel panic with a formatted message.
 *
 * @param fmt The format string.
 * @param ... Additional arguments to be formatted.
 */
void panicf(const char *fmt, ...) {
    va_list args;

    panic_i = 0; // Reset panic buffer index

    va_start(args, fmt);
    vstrfmt(panicbuf_putc, fmt, args);
    va_end(args);

    panicbuf[panic_i] = '\0'; // Null-terminate the string

    panic(panicbuf); // Call the main panic handler
}

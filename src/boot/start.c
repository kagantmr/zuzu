#include "drivers/uart.h"

void start(void) {
    uart_puts("Keep it classy yall!");
    uart_putc('\n');
    uart_puts("zuzuMicrokernel is booting..?");
    while (1);      // Loop forever
}

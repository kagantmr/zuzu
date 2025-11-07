#include <stdint.h>

#define UART0_BASE 0x1c090000

void start(void) {
    volatile uint32_t *UART0 = (uint32_t *)UART0_BASE;
    *UART0 = 'A';
    while (1);  // Infinite loop
}
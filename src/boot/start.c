#include <stdint.h>

#define UART0_BASE 0x09000000UL  // PL011 UART base for QEMU virt board

void start(void) {
    volatile uint32_t *UART0 = (uint32_t *)UART0_BASE;
    *UART0 = 'A';   // Transmit a single character
    while (1);      // Loop forever
}

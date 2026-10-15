#include "drivers/driver.h"
#include <arch/platform.h>
#include <stdint.h>

#include BOARD_BOOT_H

#define EARLY_UART_FR (0x18u / 4u)
#define EARLY_UART_TXFF (1u << 5)

void ArchEarlyPutc(char c)
{
    volatile uint32_t *uart = (volatile uint32_t *)(uintptr_t)BOARD_UART0_BASE;

    if (c == '\n')
        ArchEarlyPutc('\r');
    while (uart[EARLY_UART_FR] & EARLY_UART_TXFF)
        ;
    uart[0] = (uint32_t)(uint8_t)c;
}

void ArchPlatformInitDevices(void) { DriverProbeAll(); }

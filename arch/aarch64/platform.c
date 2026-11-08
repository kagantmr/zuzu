/**
 * @file
 * @brief AArch64 platform hooks: early UART output and device probing.
 */

#include <arch/platform.h>
#include "drivers/driver.h"
#include "kernel/mm/vmm/vmm.h"
#include <stdint.h>

#include BOARD_BOOT_H

#define EARLY_UART_FR (0x18u / 4u) /**< Word index of the PL011 flag register. */
#define EARLY_UART_TXFF (1u << 5) /**< PL011 flag: transmit FIFO full. */

void ArchEarlyPutc(char c)
{
    volatile uint32_t *uart = (volatile uint32_t *)PA_TO_VA(BOARD_UART0_BASE);

    if (c == '\n')
        ArchEarlyPutc('\r');
    while (uart[EARLY_UART_FR] & EARLY_UART_TXFF)
        ;
    uart[0] = (uint32_t)(uint8_t)c;
}

void ArchPlatformInitDevices(void) { DriverProbeAll(); }

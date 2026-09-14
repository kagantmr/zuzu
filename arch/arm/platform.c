#include "drivers/driver.h"
#include <arch/platform.h>
#include <stdint.h>

#include BOARD_BOOT_H

uint32_t rtc_epoch;

#define EARLY_UART_FR	(0x18u / 4u)
#define EARLY_UART_TXFF (1u << 5)

void arch_early_putc(char c)
{
	volatile uint32_t *uart = (volatile uint32_t *)(uintptr_t)BOARD_UART0_BASE;

	if (c == '\n')
		arch_early_putc('\r');
	while (uart[EARLY_UART_FR] & EARLY_UART_TXFF)
		;
	uart[0] = (uint32_t)(uint8_t)c;
}

void arch_platform_init_devices(void) { DriverProbeAll(); }

#include "drivers/uart/pl011.h"
#include "drivers/uart/uart.h"
#include "core/assert.h"

#include <stdint.h>
#include <stddef.h>

#define PL011_DR    0x00
#define PL011_FR    0x18
#define PL011_IBRD  0x24
#define PL011_FBRD  0x28
#define PL011_LCRH  0x2C
#define PL011_CR    0x30
#define PL011_ICR   0x44

#define FR_TXFF     (1u << 5)

#define LCRH_FEN    (1u << 4)
#define LCRH_WLEN_8 (3u << 5)

#define CR_UARTEN   (1u << 0)
#define CR_TXE      (1u << 8)
#define CR_RXE      (1u << 9)

static uintptr_t pl011_base;

static inline volatile uint32_t *pl011_reg(uint32_t offset) {
	return (volatile uint32_t *)(pl011_base + offset);
}

static inline int pl011_tx_full(void) {
	return (*pl011_reg(PL011_FR) & FR_TXFF) != 0;
}

void pl011_init(uintptr_t base_addr) {
	kassert(base_addr != 0);
	pl011_base = base_addr;

	// Leave baud divisors as provided by firmware; just ensure 8N1 + FIFO and enable TX/RX.
	*pl011_reg(PL011_CR) = 0;
	*pl011_reg(PL011_LCRH) = LCRH_FEN | LCRH_WLEN_8;
	*pl011_reg(PL011_CR) = CR_UARTEN | CR_TXE | CR_RXE;
	*pl011_reg(PL011_ICR) = 0x7FF; // Clear pending interrupts
}

void pl011_putc(char c) {
	kassert(pl011_base != 0);
	while (pl011_tx_full()) {
		// spin
	}
	*pl011_reg(PL011_DR) = (uint32_t)c;
}

int pl011_puts(const char *string) {
	kassert(string != NULL);
	while (*string) {
		pl011_putc(*string++);
	}
	return UART_OK;
}

const struct uart_driver pl011_driver = {
	.init = pl011_init,
	.putc = pl011_putc,
	.puts = pl011_puts,
};

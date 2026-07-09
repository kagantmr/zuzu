// pl011.c - ARM PL011 UART driver implementation

#include "drivers/uart/pl011.h"
#include "drivers/uart/uart.h"
#include <assert.h>

#include <stdint.h>
#include <stddef.h>

static volatile struct {
    char buf[UART_RX_BUF_SIZE];
    uint16_t head;  // ISR writes here
    uint16_t tail;  // Consumer reads here
} rx_ring;

static uintptr_t pl011_base;

static inline volatile uint32_t *pl011_reg(uint32_t offset) {
	return (volatile uint32_t *)(pl011_base + offset);
}


static inline int pl011_tx_full(void) {
	return (*pl011_reg(PL011_FR) & FR_TXFF) != 0;
}


void pl011_init(uintptr_t base_addr) {
	assert(base_addr != 0);
	pl011_base = base_addr;

	// Leave baud divisors as provided by firmware; just ensure 8N1 + FIFO and enable TX/RX.
	*pl011_reg(PL011_CR) = 0;
	*pl011_reg(PL011_LCRH) = LCRH_FEN | LCRH_WLEN_8;
	*pl011_reg(PL011_CR) = CR_UARTEN | CR_TXE | CR_RXE;
	*pl011_reg(PL011_ICR) = 0x7FF; // Clear pending interrupts
}

static void pl011_putc_raw(char c) {
	while (pl011_tx_full()) {
		// spin
	}
	*pl011_reg(PL011_DR) = (uint32_t)c;
}

void pl011_putc(char c) {
	// assert(pl011_base != 0);
	// A raw serial line (unlike QEMU's host-terminal pty, which has ONLCR)
	// has no one to turn LF into CRLF, so do it here or every line after
	// the first drifts right on the receiving terminal.
	if (c == '\n')
		pl011_putc_raw('\r');
	pl011_putc_raw(c);
}

int pl011_puts(const char *string) {
	//assert(string != NULL);
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

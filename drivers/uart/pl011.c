#include "drivers/uart/pl011.h"
#include "arch/arm/include/irq.h" 
#include "drivers/uart/uart.h"
#include "core/assert.h"

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

static void pl011_irq_handler(void *ctx) {
    (void)ctx;
    
    // Read all available characters
    while (!(*pl011_reg(PL011_FR) & FR_RXFE)) {
        char c = (char)(*pl011_reg(PL011_DR) & 0xFF);
        uint16_t next = (rx_ring.head + 1) % UART_RX_BUF_SIZE;
        if (next != rx_ring.tail) {
            rx_ring.buf[rx_ring.head] = c;
            rx_ring.head = next;
        }
        // else: drop character if buffer full
    }
    
    // Clear RX interrupt
    *pl011_reg(PL011_ICR) = IMSC_RXIM;
}

static inline int pl011_tx_full(void) {
	return (*pl011_reg(PL011_FR) & FR_TXFF) != 0;
}

void pl011_init_irq(uintptr_t base_addr) {
    // Assumes pl011_init() already called
    (void)base_addr;
    
    // Enable RX interrupt
    *pl011_reg(PL011_IMSC) = IMSC_RXIM;
    
    // Register with IRQ subsystem
    // PL011 UART0 is IRQ 37 (SPI 5 + 32) on Vexpress
    irq_register(37, pl011_irq_handler, (void *)pl011_base);
    irq_enable_line(37);
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

int pl011_getc(void) {
    if (rx_ring.head == rx_ring.tail) {
        return -1;  // Empty
    }
    char c = rx_ring.buf[rx_ring.tail];
    rx_ring.tail = (rx_ring.tail + 1) % UART_RX_BUF_SIZE;
    return (int)(unsigned char)c;
}

bool pl011_has_rx(void) {
    return rx_ring.head != rx_ring.tail;
}

const struct uart_driver pl011_driver = {
    .init = pl011_init,
    .putc = pl011_putc,
    .puts = pl011_puts,
    .getc = pl011_getc,  
};

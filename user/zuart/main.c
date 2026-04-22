#include "zuart.h"
#include "zuzu/protocols/zuart_protocol.h"
#include "zuzu/protocols/devmgr_protocol.h"
#include "zuzu/protocols/nt_protocol.h"
#include "zuzu/ipcx.h"
#include <stdint.h>
#include <string.h>
#include <mem.h>

volatile pl011_t *uart;
int port;
static int32_t devmgr_port = -1;
static int32_t serial_dev_handle = -1;
static int32_t serial_irq_ntfn = -1;
ringbuf_t rxrb, txrb;

#define ZUART_DEV_CLASS DEV_CLASS_SERIAL
#define ZUART_COMPATIBLE "arm,pl011"

static void uart_txraw(char c)
{
    if (!(uart->FR & FR_TXFF) && rb_empty(&txrb)) {
        uart->DR = c;
    } else if (rb_write(&txrb, c)) {
        uart->IMSC |= IMSC_TXIM;
    }
}

static void uart_txbyte(char c)
{
    // Console-friendly newline handling: terminals typically expect CRLF.
    if (c == '\n') {
        uart_txraw('\r');
    }
    uart_txraw(c);
}



static void drain_uart_rx_fifo(void)
{
    while (!(uart->FR & FR_RXFE) && !rb_full(&rxrb)) {
        char c = (char)(uart->DR & 0xFF);
        rb_write(&rxrb, c);
    }
}

static int32_t wait_for_devmgr(void)
{
    while (1) {
        zuzu_ipcmsg_t ntmsg = _call(NT_PORT, NT_LOOKUP, nt_pack("devm"), 0);
        if ((int32_t)ntmsg.r1 == NT_LU_OK) {
            devmgr_port = (int32_t)ntmsg.r2;
            return (int32_t)ntmsg.r3;
        }
        _sleep(10);
    }
}


static int32_t request_serial_device(void)
{
    while (1) {
        zuzu_ipcmsg_t devmsg = _call(devmgr_port, DEV_REQUEST, DEV_CLASS_SERIAL, 0);
        if ((int32_t)devmsg.r1 == 0) {
            return (int32_t)devmsg.r2;
        }
        _sleep(10);
    }
}

static void handle_irq_event(void)
{
    if (uart->MIS & (IMSC_RXIM | IMSC_RTIM))
    {
        drain_uart_rx_fifo();
        uart->ICR = (IMSC_RXIM | IMSC_RTIM);
    }
    if (uart->MIS & IMSC_TXIM)
    {
        while (!(uart->FR & FR_TXFF) && !rb_empty(&txrb))
            uart->DR = rb_read(&txrb);
        if (rb_empty(&txrb))
            uart->IMSC &= ~IMSC_TXIM;
        uart->ICR = IMSC_TXIM;
    }
    _irq_done((uint32_t)serial_dev_handle);
}

static void handle_client_message(zuzu_ipcmsg_t msg)
{
    if (msg.r2 == 0)
    {
        /* One-way IPCX send: r1 carries the byte count. */
        uint32_t len = msg.r1;
        if (len > IPCX_BUF_SIZE) len = IPCX_BUF_SIZE;

        char *ipcx_buf = (char *)IPCX_BUF;
        for (uint32_t i = 0; i < len; i++)
            uart_txbyte(ipcx_buf[i]);
        return;
    }

    {
        /* IPCX call: r2 carries the requested byte count and r0 is the reply handle. */
        uint32_t reply_handle = (uint32_t)msg.r0;
        uint32_t len = msg.r2;
        if (len > IPCX_BUF_SIZE) len = IPCX_BUF_SIZE;

        if (rb_empty(&rxrb)) {
            drain_uart_rx_fifo();
        }

        /* Block in server until at least one byte arrives */
        while (rb_empty(&rxrb)) {
            drain_uart_rx_fifo();
        }

        /* Write available data to IPCX buffer (kernel will copy to caller) */
        char *ipcx_buf = (char *)IPCX_BUF;
        uint32_t n = 0;
        while (!rb_empty(&rxrb) && n < len)
            ipcx_buf[n++] = rb_read(&rxrb);

        /* Reply with IPCX buffer contents and byte count. */
        (void)_replyx(reply_handle, (uint32_t)n);
    }
}

int zuart_setup(void)
{
    port = _port_create();
    if (port < 0) {
        return ZUART_INIT_FAIL;
    }

    int32_t nt_slot = _port_grant(port, NAMETABLE_PID);
    if (nt_slot < 0) {
        return ZUART_INIT_FAIL;
    }

    (void)wait_for_devmgr();

    int32_t dev_handle = request_serial_device();

    serial_irq_ntfn = _ntfn_create();
    if (serial_irq_ntfn < 0) {
        return ZUART_INIT_FAIL;
    }

    if (_irq_claim(dev_handle) < 0) {
        return ZUART_INIT_FAIL;
    }
    if (_irq_bind(dev_handle, (uint32_t)serial_irq_ntfn) < 0) {
        return ZUART_INIT_FAIL;
    }

    serial_dev_handle = dev_handle;
    uart = (volatile pl011_t *)_mapdev(dev_handle);
    if ((intptr_t)uart <= 0) {
        return ZUART_INIT_FAIL;
    }

    rxrb.head = rxrb.tail = 0;
    txrb.head = txrb.tail = 0;

    // Deterministic UART interrupt setup: mask/clear, configure FIFO threshold,
    // then enable UART and finally unmask RX sources.
    uart->IMSC = 0;
    uart->CR = 0;
    uart->ICR = ICR_ALL;
    uart->IFLS = (uart->IFLS & ~IFLS_RX_MASK) | IFLS_RX_1_8;
    uart->LCRH = LCRH_FEN | LCRH_WLEN_8;
    uart->CR = CR_UARTEN | CR_TXE | CR_RXE;
    uart->ICR = ICR_ALL;
    uart->IMSC = (IMSC_RXIM | IMSC_RTIM);

    (void)_send(NT_PORT, NT_REGISTER, nt_pack("uart"), (uint32_t)nt_slot);
    return ZUART_INIT_OK;
}

int main(void)
{
    int exit_code;
    if ((exit_code = zuart_setup()) != 0)
        return exit_code;

    zuzu_ipcmsg_t msg;

    while (1)
    {
        int32_t bits = _ntfn_poll((uint32_t)serial_irq_ntfn);
        if (bits > 0) {
            handle_irq_event();
        }

        msg = _recv(port);

        if (msg.r0 > 0) {
            handle_client_message(msg);
        }
    }
}

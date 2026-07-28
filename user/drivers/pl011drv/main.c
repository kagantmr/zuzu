#include "pl011drv.h"
#include "zuzu/protocols/uart_protocol.h"
#include "zuzu/protocols/devmgr_protocol.h"
#include "zuzu/protocols/nt_protocol.h"
#include <zuzu/protocols/sysd_protocol.h>
#include "zuzu/lmsg.h"
#include <ring.h>
#include <zuzu/channel.h>
#include <stdint.h>
#include <string.h>
#include <mem.h>

#define PL011DRV_DEV_CLASS DEV_CLASS_SERIAL
#define PL011DRV_COMPATIBLE "arm,pl011"

static volatile pl011_t *uart;
static Handle client_port = -1;
static Handle devmgr_port = -1;
static Handle serial_dev_handle = -1;
static Handle serial_irq_ntfn = -1;
static ring_t rxrb, txrb;
static uint8_t rxbuf_storage[UART_RINGBUF_MAX];
static uint8_t txbuf_storage[UART_RINGBUF_MAX];

static void uart_txraw(char c)
{
    if (!(uart->FR & FR_TXFF) && ring_avail(&txrb) == 0) {
        uart->DR = (uint32_t)c;
    } else if (ring_push(&txrb, (uint8_t)c) == 0) {
        uart->IMSC |= IMSC_TXIM;
    }
}

static void uart_txbyte(char c)
{
    if (c == '\n') {
        uart_txraw('\r');
    }
    uart_txraw(c);
}

static void drain_uart_rx_fifo(void)
{
    while (!(uart->FR & FR_RXFE) && ring_full(&rxrb) == 0) {
        uint8_t c = (uint8_t)(uart->DR & 0xFF);
        (void)ring_push(&rxrb, c);
    }
}

static int32_t wait_for_devmgr(void)
{
    while (1) {
        Message ntmsg = zuzu_msg_call(NT_PORT, NT_LOOKUP, nt_pack("devm"), 0);
        if ((int32_t)ntmsg.w1 == NT_LU_OK) {
            devmgr_port = (int32_t)ntmsg.w2;
            return (int32_t)ntmsg.w3;
        }
        zuzu_sleep(10);
    }
}

static int32_t request_serial_device(void)
{
    while (1) {
        Message devmsg = zuzu_msg_call(devmgr_port, DEV_REQUEST, DEV_CLASS_SERIAL, 0);
        if ((int32_t)devmsg.w1 == 0) {
            return (int32_t)devmsg.w2;
        }
        zuzu_sleep(10);
    }
}

static void handle_irq_event(void)
{
    if (uart->MIS & (IMSC_RXIM | IMSC_RTIM)) {
        drain_uart_rx_fifo();
        uart->ICR = (IMSC_RXIM | IMSC_RTIM);
    }
    if (uart->MIS & IMSC_TXIM) {
        while (!(uart->FR & FR_TXFF) && ring_avail(&txrb) > 0) {
            uint8_t b = 0;
            if (ring_pop(&txrb, &b) == 0)
                uart->DR = (uint32_t)b;
            else
                break;
        }
        if (ring_avail(&txrb) == 0)
            uart->IMSC &= ~IMSC_TXIM;
        uart->ICR = IMSC_TXIM;
    }
    zuzu_irq_done((uint32_t)serial_dev_handle);
}

/* zuzu_msg_lsend(client_port, len): fire-and-forget write, payload in lmsg_buf(). */
static void handle_write(uint32_t len)
{
    if (len > LMSG_BUF_SIZE)
        len = LMSG_BUF_SIZE;

    const char *buf = lmsg_buf();
    for (uint32_t i = 0; i < len; i++)
        uart_txbyte(buf[i]);
}

/* zuzu_msg_lcall(client_port, max_len): read up to max_len bytes already
 * buffered from the UART; replies immediately with however many are
 * available (possibly zero) rather than blocking for more. */
static void handle_read(Handle reply_handle, uint32_t max_len)
{
    if (max_len > LMSG_BUF_SIZE)
        max_len = LMSG_BUF_SIZE;

    drain_uart_rx_fifo();

    char *buf = (char *)lmsg_buf();
    uint32_t n = 0;
    while (n < max_len && ring_avail(&rxrb) > 0) {
        uint8_t b = 0;
        if (ring_pop(&rxrb, &b) != 0)
            break;
        buf[n++] = (char)b;
    }

    (void)chan_reply(reply_handle, buf, n);
}

int pl011drv_setup(void)
{
    client_port = zuzu_port_create();
    if (client_port < 0) {
        return client_port;
    }

    int32_t nt_slot = zuzu_grant(client_port, NAMETABLE_PID);
    if (nt_slot < 0) {
        return nt_slot;
    }

    (void)wait_for_devmgr();

    int32_t dev_handle = request_serial_device();

    serial_irq_ntfn = zuzu_ntfn_create();
    if (serial_irq_ntfn < 0) {
        return serial_irq_ntfn;
    }

    int32_t bind_rc = zuzu_irq_bind(dev_handle, (uint32_t)serial_irq_ntfn);
    if (bind_rc < 0) {
        return bind_rc;
    }

    serial_dev_handle = dev_handle;
    uart = (volatile pl011_t *)zuzu_memmap(dev_handle, 0, VM_PROT_RW, 0);
    if ((intptr_t)uart <= 0) {
        return (int)(intptr_t)uart;
    }

    ring_init(&rxrb, rxbuf_storage, UART_RINGBUF_MAX);
    ring_init(&txrb, txbuf_storage, UART_RINGBUF_MAX);

    uart->IMSC = 0;
    uart->CR = 0;
    uart->ICR = ICR_ALL;
    uart->IFLS = (uart->IFLS & ~IFLS_RX_MASK) | IFLS_RX_1_8;
    uart->LCRH = LCRH_FEN | LCRH_WLEN_8;
    uart->CR = CR_UARTEN | CR_TXE | CR_RXE;
    uart->ICR = ICR_ALL;
    uart->IMSC = (IMSC_RXIM | IMSC_RTIM);

    (void)zuzu_msg_send(NT_PORT, NT_REGISTER, nt_pack("pl011drv"), (uint32_t)nt_slot);
    return PL011DRV_INIT_OK;
}

int main(void)
{
    int exit_code;
    if ((exit_code = pl011drv_setup()) != 0)
        return exit_code;

    enum { H_IRQ = 0, H_PORT = 1 };
    Handle handles[] = {
        [H_IRQ]  = serial_irq_ntfn,
        [H_PORT] = client_port,
    };

    while (1) {
        WaitanyResult r;
        if (zuzu_waitany(handles, 2, TIMEOUT_INFINITE, &r) != 0)
            continue;

        switch (r.kind) {
        case WAITANY_KIND_NTFN:
            handle_irq_event();
            break;
        case WAITANY_KIND_SEND:
            handle_write(r.w1);
            break;
        case WAITANY_KIND_CALL:
            handle_read((Handle)r.source, r.w2);
            break;
        default:
            break;
        }
    }
}

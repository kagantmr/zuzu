#include "pl011drv.h"
#include "zuzu/protocols/uart_protocol.h"
#include "zuzu/protocols/devmgr_protocol.h"
#include "zuzu/protocols/nt_protocol.h"
#include <zuzu/protocols/sysd_protocol.h>
#include "zuzu/ipcx.h"
#include <zuzu/ring.h>
#include <zuzu/channel.h>
#include <stdint.h>
#include <string.h>
#include <mem.h>

volatile pl011_t *uart;
int port;
static int32_t devmgr_port = -1;
static int32_t serial_dev_handle = -1;
static int32_t serial_irq_ntfn = -1;
static ring_t rxrb, txrb;
static uint8_t rxbuf_storage[UART_RINGBUF_MAX];
static uint8_t txbuf_storage[UART_RINGBUF_MAX];

#define PL011DRV_DEV_CLASS DEV_CLASS_SERIAL
#define PL011DRV_COMPATIBLE "arm,pl011"
#define PL011DRV_RECV_SLICE_MS 5u

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
        msg_t ntmsg = _call(NT_PORT, NT_LOOKUP, nt_pack("devm"), 0);
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
        msg_t devmsg = _call(devmgr_port, DEV_REQUEST, DEV_CLASS_SERIAL, 0);
        if ((int32_t)devmsg.r1 == 0) {
            return (int32_t)devmsg.r2;
        }
        _sleep(10);
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
    _irq_done((uint32_t)serial_dev_handle);
}

static void service_pending_irq(void)
{
    int32_t bits = _ntfn_poll((uint32_t)serial_irq_ntfn);
    if (bits > 0) {
        handle_irq_event();
    }
}

static void handle_client_message(msg_t msg)
{
    if (msg.r2 == 0) {
        uint32_t len = msg.r1;
        if (len > IPCX_BUF_SIZE) len = IPCX_BUF_SIZE;

        char *buf = (char *)ipcx_buf();
        for (uint32_t i = 0; i < len; i++)
            uart_txbyte(buf[i]);
        return;
    }

    uint32_t reply_handle = (uint32_t)msg.r0;
    uint32_t len = msg.r2;
    if (len > IPCX_BUF_SIZE) len = IPCX_BUF_SIZE;

    service_pending_irq();
    drain_uart_rx_fifo();
    char *buf = (char *)ipcx_buf();
    uint32_t n = 0;
    while (ring_avail(&rxrb) > 0 && n < len) {
        uint8_t b = 0;
        if (ring_pop(&rxrb, &b) == 0)
            buf[n++] = (char)b;
        else
            break;
    }

    (void)chan_reply((handle_t)reply_handle, buf, (uint32_t)n);
}

int pl011drv_setup(void)
{
    port = _ep_create();
    if (port < 0) {
        return PL011DRV_INIT_FAIL;
    }

    int32_t nt_slot = _cap_grant(port, NAMETABLE_PID);
    if (nt_slot < 0) {
        return PL011DRV_INIT_FAIL;
    }

    (void)wait_for_devmgr();

    int32_t dev_handle = request_serial_device();

    serial_irq_ntfn = _ntfn_create();
    if (serial_irq_ntfn < 0) {
        return PL011DRV_INIT_FAIL;
    }

    if (_irq_claim(dev_handle) < 0) {
        return PL011DRV_INIT_FAIL;
    }
    if (_irq_bind(dev_handle, (uint32_t)serial_irq_ntfn) < 0) {
        return PL011DRV_INIT_FAIL;
    }

    serial_dev_handle = dev_handle;
    uart = (volatile pl011_t *)_mapdev(dev_handle);
    if ((intptr_t)uart <= 0) {
        return PL011DRV_INIT_FAIL;
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

    (void)_send(NT_PORT, NT_REGISTER, nt_pack("pl011drv"), (uint32_t)nt_slot);
    return PL011DRV_INIT_OK;
}

int main(void)
{
    int exit_code;
    if ((exit_code = pl011drv_setup()) != 0)
        return exit_code;

    /*
    const char *startup_banner = "pl011drv: online\n";
    for (const char *p = startup_banner; *p; p++)
        uart_txbyte(*p);
    */

    msg_t msg;

    while (1) {
        service_pending_irq();

        msg = _recv_timeout(port, PL011DRV_RECV_SLICE_MS);
        if (msg.r0 >= 0) {
            handle_client_message(msg);
        }
    }
}

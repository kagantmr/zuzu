#include "zuart.h"
#include "zuzu/protocols/zuart_protocol.h"
#include "zuzu/protocols/devmgr_protocol.h"
#include "zuzu/protocols/nt_protocol.h"
#include <stdint.h>
#include <string.h>
#include <mem.h>
#include <stdio.h>

volatile pl011_t *uart;
int port;
static int32_t devmgr_port = -1;
static int32_t serial_dev_handle = -1;
ringbuf_t rxrb, txrb;

#define ZUART_WAITQ_MAX 64
#define ZUART_DEV_CLASS DEV_CLASS_SERIAL
#define ZUART_COMPATIBLE "arm,pl011"
#define ZUART_TX_SHMEM_SIZE 4096

static zuart_waiting_t read_waitq[ZUART_WAITQ_MAX];
static uint16_t read_waitq_head, read_waitq_tail;
static char    *cached_buf    = NULL;
static int32_t  cached_handle = -1;
static int32_t tx_shmem_handle = -1;
static char *tx_shmem_buf = NULL;

#define LOG_LIT(s) printf("%s", (s))

static int attach_cached_handle(int32_t handle)
{
    if (handle != cached_handle) {
        cached_buf = (char *)_attach(handle);
        cached_handle = handle;
    }

    if ((intptr_t)cached_buf <= 0) {
        cached_handle = -1;
        return 0;
    }
    return 1;
}

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

static inline int waitq_empty(void) { return read_waitq_head == read_waitq_tail; }
static inline int waitq_full(void)  { return (uint16_t)((read_waitq_head + 1) % ZUART_WAITQ_MAX) == read_waitq_tail; }

static inline int waitq_push(uint32_t reply_handle, int32_t handle, size_t len)
{
    if (waitq_full()) return 0;
    read_waitq[read_waitq_head].reply_handle = reply_handle;
    read_waitq[read_waitq_head].shmem_handle = handle;
    read_waitq[read_waitq_head].length       = len;
    read_waitq_head = (uint16_t)((read_waitq_head + 1) % ZUART_WAITQ_MAX);
    return 1;
}

static inline zuart_waiting_t waitq_pop(void)
{
    zuart_waiting_t w = read_waitq[read_waitq_tail];
    read_waitq_tail = (uint16_t)((read_waitq_tail + 1) % ZUART_WAITQ_MAX);
    return w;
}

static void service_read_waiters(void)
{
    while (!waitq_empty() && !rb_empty(&rxrb))
    {
        zuart_waiting_t waiter = waitq_pop();
        if (attach_cached_handle(waiter.shmem_handle)) {
            char *buf = cached_buf;
            size_t n = 0;
            while (!rb_empty(&rxrb) && n < waiter.length)
                buf[n++] = rb_read(&rxrb);
            _reply(waiter.reply_handle, n, 0, 0);
        } else {
            _reply(waiter.reply_handle, 0, ZUART_ERR, 0);
        }
    }
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
        LOG_LIT("zuart: waiting for devmgr registration\n");
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
        if ((int32_t)devmsg.r1 == ERR_NOENT) {
            LOG_LIT("zuart: DEV_REQUEST no matching device/tag yet\n");
        } else if ((int32_t)devmsg.r1 == ERR_NOPERM) {
            LOG_LIT("zuart: DEV_REQUEST noperm (wrong devmgr instance)\n");
        } else if ((int32_t)devmsg.r1 == ERR_BUSY) {
            LOG_LIT("zuart: DEV_REQUEST busy, retrying\n");
        } else {
            LOG_LIT("zuart: DEV_REQUEST failed, retrying\n");
        }
        _sleep(10);
    }
}

static void handle_irq_event(void)
{
    if (uart->MIS & (IMSC_RXIM | IMSC_RTIM))
    {
        drain_uart_rx_fifo();
        service_read_waiters();
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
    uint32_t reply_handle = (uint32_t)msg.r0;
    uint32_t sender_pid = msg.r1;
    uint32_t command = msg.r2;
    uint32_t packed = msg.r3;

    switch (command)
    {
    case ZUART_CMD_WRITE:
    {
        int32_t handle = zuart_arg_handle(packed);
        size_t len = zuart_arg_len(packed);
        if (!attach_cached_handle(handle)) {
            _reply(reply_handle, 0, ZUART_ERR, 0);
            break;
        }
        if (len > ZUART_TX_SHMEM_SIZE) {
            len = ZUART_TX_SHMEM_SIZE;
        }
        for (size_t i = 0; i < len; i++)
            uart_txbyte(cached_buf[i]);
        _reply(reply_handle, len, ZUART_SEND_OK, 0);
    }
    break;

    case ZUART_CMD_GET_SHMEM:
    {
        if (tx_shmem_handle < 0) {
            _reply(reply_handle, ZUART_ERR, 0, 0);
            break;
        }
        int32_t granted = _port_grant(tx_shmem_handle, (int32_t)sender_pid);
        if (granted < 0) {
            _reply(reply_handle, ZUART_ERR, 0, 0);
            break;
        }
        _reply(reply_handle, ZUART_SEND_OK, (uint32_t)granted, 0);
    }
    break;

    case ZUART_CMD_WRITE_TXBUF:
    {
        size_t len = packed;
        if (tx_shmem_buf == NULL) {
            _reply(reply_handle, 0, ZUART_ERR, 0);
            break;
        }
        if (len > ZUART_TX_SHMEM_SIZE) {
            len = ZUART_TX_SHMEM_SIZE;
        }
        for (size_t i = 0; i < len; i++)
            uart_txbyte(tx_shmem_buf[i]);
        _reply(reply_handle, len, ZUART_SEND_OK, 0);
    }
    break;

    case ZUART_CMD_READ:
    {
        int32_t handle = zuart_arg_handle(packed);
        size_t requested = zuart_arg_len(packed);

        if (rb_empty(&rxrb)) {
            drain_uart_rx_fifo();
        }

        // Fallback for setups where RX IRQ delivery is unavailable/unreliable.
        // Block in the device server until at least one byte arrives.
        while (rb_empty(&rxrb)) {
            drain_uart_rx_fifo();
        }

        if (!rb_empty(&rxrb)) {
            if (!attach_cached_handle(handle)) {
                _reply(reply_handle, 0, ZUART_ERR, 0);
                break;
            }
            size_t n = 0;
            while (!rb_empty(&rxrb) && n < requested)
                cached_buf[n++] = rb_read(&rxrb);
            _reply(reply_handle, n, 0, 0);
        } else {
            if (!waitq_push(reply_handle, handle, requested))
                _reply(reply_handle, 0, ZUART_ERR, 0);
        }
    }
    break;

    default:
        _reply(reply_handle, 0, ZUART_ERR, 0);
        break;
    }
}

int zuart_setup(void)
{
    port = _port_create();
    if (port < 0) {
        LOG_LIT("zuart: _port_create failed\n");
        return ZUART_INIT_FAIL;
    }

    int32_t nt_slot = _port_grant(port, NAMETABLE_PID);
    if (nt_slot < 0) {
        LOG_LIT("zuart: _port_grant to nametable failed\n");
        return ZUART_INIT_FAIL;
    }

    (void)wait_for_devmgr();

    int32_t dev_handle = request_serial_device();

    if (_irq_claim(dev_handle) < 0) {
        LOG_LIT("zuart: _irq_claim failed\n");
        return ZUART_INIT_FAIL;
    }
    if (_irq_bind(dev_handle, (uint32_t)port) < 0) {
        LOG_LIT("zuart: _irq_bind failed\n");
        return ZUART_INIT_FAIL;
    }

    serial_dev_handle = dev_handle;
    uart = (volatile pl011_t *)_mapdev(dev_handle);
    if ((intptr_t)uart <= 0) {
        LOG_LIT("zuart: _mapdev failed\n");
        return ZUART_INIT_FAIL;
    }

    shmem_result_t tx_shm = _memshare(ZUART_TX_SHMEM_SIZE);
    if (tx_shm.handle < 0 || tx_shm.addr == NULL) {
        LOG_LIT("zuart: _memshare for tx buffer failed\n");
        return ZUART_INIT_FAIL;
    }
    tx_shmem_handle = tx_shm.handle;
    tx_shmem_buf = (char *)tx_shm.addr;

    rxrb.head = rxrb.tail = 0;
    txrb.head = txrb.tail = 0;
    read_waitq_head = read_waitq_tail = 0;

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
        msg = _recv(port);

        if (msg.r0 == 0) {
            handle_irq_event();
        } else if (msg.r0 > 0) {
            handle_client_message(msg);
        }
    }
}

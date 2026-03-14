#include "zuart.h"
#include "zuzu/protocols/zuart_protocol.h"
#include "zuzu/protocols/nt_protocol.h"
#include <stdint.h>

volatile pl011_t *uart;
int port;
ringbuf_t rxrb, txrb;

#define ZUART_WAITQ_MAX 64
static zuart_waiting_t read_waitq[ZUART_WAITQ_MAX];
static uint16_t read_waitq_head, read_waitq_tail;
static char    *cached_buf    = NULL;
static int32_t  cached_handle = -1;

#define LOG_LIT(s) _log((s), sizeof(s) - 1)

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

static void uart_txbyte(char c)
{
    if (!(uart->FR & FR_TXFF) && rb_empty(&txrb)) {
        uart->DR = c;
    } else if (rb_write(&txrb, c)) {
        uart->IMSC |= IMSC_TXIM;
    }
}

static inline int waitq_empty(void) { return read_waitq_head == read_waitq_tail; }
static inline int waitq_full(void)  { return (uint16_t)((read_waitq_head + 1) % ZUART_WAITQ_MAX) == read_waitq_tail; }

static inline int waitq_push(int32_t pid, int32_t handle, size_t len)
{
    if (waitq_full()) return 0;
    read_waitq[read_waitq_head].pid          = (uint32_t)pid;
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
            _reply(waiter.pid, n, 0, 0);
        } else {
            _reply(waiter.pid, 0, ZUART_ERR, 0);
        }
    }
}

static int32_t wait_for_bootstrap_handle(void)
{
    while (1) {
        zuzu_ipcmsg_t msg = _recv(port);

        if (msg.r0 > 0 && msg.r1 == ZUART_CMD_BOOTSTRAP) {
            _reply(msg.r0, ZUART_SEND_OK, 0, 0);
            return (int32_t)msg.r2;
        }

        if (msg.r0 > 0) {
            _reply(msg.r0, ZUART_ERR, 0, 0);
        }
        if (msg.r0 == 0) {
            _irq_done(UART0_IRQ_NUM);
        }
    }
}

static void handle_irq_event(void)
{
    if (uart->MIS & IMSC_RXIM)
    {
        while (!(uart->FR & FR_RXFE) && !rb_full(&rxrb))
        {
            char c = (char)(uart->DR & 0xFF);
            rb_write(&rxrb, c);
            if (c >= 0x20 && c < 0x7f)
                uart_txbyte(c);
        }
        service_read_waiters();
        uart->ICR = IMSC_RXIM;
    }
    if (uart->MIS & IMSC_TXIM)
    {
        while (!(uart->FR & FR_TXFF) && !rb_empty(&txrb))
            uart->DR = rb_read(&txrb);
        if (rb_empty(&txrb))
            uart->IMSC &= ~IMSC_TXIM;
        uart->ICR = IMSC_TXIM;
    }
    _irq_done(UART0_IRQ_NUM);
}

static void handle_client_message(zuzu_ipcmsg_t msg)
{
    switch (msg.r1)
    {
    case ZUART_CMD_WRITE:
    {
        int32_t handle = (int32_t)msg.r2;
        size_t len = msg.r3;
        if (!attach_cached_handle(handle)) {
            _reply(msg.r0, 0, ZUART_ERR, 0);
            break;
        }
        for (size_t i = 0; i < len; i++)
            uart_txbyte(cached_buf[i]);
        _reply(msg.r0, len, ZUART_SEND_OK, 0);
    }
    break;

    case ZUART_CMD_READ:
    {
        int32_t handle = (int32_t)msg.r2;
        size_t requested = msg.r3;
        if (!rb_empty(&rxrb)) {
            if (!attach_cached_handle(handle)) {
                _reply(msg.r0, 0, ZUART_ERR, 0);
                break;
            }
            size_t n = 0;
            while (!rb_empty(&rxrb) && n < requested)
                cached_buf[n++] = rb_read(&rxrb);
            _reply(msg.r0, n, 0, 0);
        } else {
            if (!waitq_push(msg.r0, handle, requested))
                _reply(msg.r0, 0, ZUART_ERR, 0);
        }
    }
    break;

    default:
        _reply(msg.r0, 0, ZUART_ERR, 0);
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

    zuzu_ipcmsg_t reg = _call(NT_PORT, NT_REGISTER, nt_pack("uart"), (uint32_t)nt_slot);
    if ((int32_t)reg.r1 != NT_REG_OK) {
        LOG_LIT("zuart: NT_REGISTER(uart) failed\n");
        return ZUART_INIT_FAIL;
    }

    int32_t dev_handle = wait_for_bootstrap_handle();

    if (_irq_claim(dev_handle) < 0) {
        LOG_LIT("zuart: _irq_claim failed\n");
        return ZUART_INIT_FAIL;
    }
    if (_irq_bind(dev_handle, (uint32_t)port) < 0) {
        LOG_LIT("zuart: _irq_bind failed\n");
        return ZUART_INIT_FAIL;
    }

    uart = (volatile pl011_t *)_mapdev(dev_handle);
    if ((intptr_t)uart <= 0) {
        LOG_LIT("zuart: _mapdev failed\n");
        return ZUART_INIT_FAIL;
    }

    rxrb.head = rxrb.tail = 0;
    txrb.head = txrb.tail = 0;
    read_waitq_head = read_waitq_tail = 0;

    uart->IMSC |= IMSC_RXIM;
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
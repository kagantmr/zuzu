#include "zuart.h"
#include "zuart_protocol.h"
#include "nt_protocol.h"
#include <stdint.h>

volatile pl011_t *uart;
int port;
ringbuf_t rxrb, txrb;

#define ZUART_WAITQ_MAX 64
static int32_t read_waitq[ZUART_WAITQ_MAX];
static uint16_t read_waitq_head, read_waitq_tail;

static inline int waitq_empty(void)
{
    return read_waitq_head == read_waitq_tail;
}

static inline int waitq_full(void)
{
    return (uint16_t)((read_waitq_head + 1) % ZUART_WAITQ_MAX) == read_waitq_tail;
}

static inline int waitq_push(int32_t pid)
{
    if (waitq_full())
        return 0;
    read_waitq[read_waitq_head] = pid;
    read_waitq_head = (uint16_t)((read_waitq_head + 1) % ZUART_WAITQ_MAX);
    return 1;
}

static inline int32_t waitq_pop(void)
{
    int32_t pid = read_waitq[read_waitq_tail];
    read_waitq_tail = (uint16_t)((read_waitq_tail + 1) % ZUART_WAITQ_MAX);
    return pid;
}

static void service_read_waiters(void)
{
    while (!waitq_empty() && !rb_empty(&rxrb))
    {
        int32_t waiter_pid = waitq_pop();
        _reply((uint32_t)waiter_pid, (uint32_t)rb_read(&rxrb), 0, 0);
    }
}

int zuart_setup(void)
{
    // first, map the device
    uart = (volatile pl011_t *)_mapdev(VEXPRESS_UART0_PA, 0x1000);
    if (!uart)
        return ZUART_INIT_FAIL;

    // claim irq and bind it to an IPC port
    if (_irq_claim(UART0_IRQ_NUM))
        return ZUART_INIT_FAIL;
    port = _port_create();
    if (port < 0)
        return ZUART_INIT_FAIL;
    if (_irq_bind(UART0_IRQ_NUM, port))
        return ZUART_INIT_FAIL;

    // grant that port to nametable
    int32_t slot = _port_grant(port, NAMETABLE_PID);
    _call(NT_PORT, NT_REGISTER, nt_pack("uart"), slot);

    // init rb
    rxrb.head = rxrb.tail = 0;
    txrb.head = txrb.tail = 0;
    read_waitq_head = read_waitq_tail = 0;

    // enable RX interrupts
    uart->IMSC |= IMSC_RXIM;
    return ZUART_INIT_OK;
}

int main(void)
{
    int exit_code;
    if ((exit_code = zuart_setup()) != 0)
    {
        return exit_code;
    }

    zuzu_ipcmsg_t returns;

    while (1)
    {
        returns = _recv(port);
        if (returns.r0 == 0)
        {
            // kernel notified about IRQ, read type of interrupt, acknowledge & process
            if (uart->MIS & IMSC_RXIM)
            {
                // RX interrupt
                while (!(uart->FR & FR_RXFE) && !rb_full(&rxrb))
                {
                    // todo: make proper input receiving instead of echoing things back
                    char c = uart->DR;
                    rb_write(&rxrb, c); // write into rx ringbuf
                    uart->DR = c;          // echo it back immediately
                }
                service_read_waiters();
                uart->ICR |= IMSC_RXIM;
            }
            if (uart->MIS & IMSC_TXIM)
            {
                // TX interrupt
                while (!(uart->FR & FR_TXFF) && !rb_empty(&txrb))
                {
                    uart->DR = rb_read(&txrb); // write FROM buffer TO hardware
                }
                if (rb_empty(&txrb))
                    uart->IMSC &= ~IMSC_TXIM;
                uart->ICR |= IMSC_TXIM;
            }
            _irq_done(UART0_IRQ_NUM);
        }
        else if (returns.r0 > 0)
        {
            // todo: When available, implement shared memory syscalls to get data
            // process wants to write/read something
            // we don't have shared memory syscalls YET so zuzu gets payload of one char per ipc message
            switch (returns.r1)
            {
            case ZUART_CMD_WRITE:
            {
                char c = returns.r2;
                size_t l = returns.r3; // not used yet
                (void)l;

                // Fast path: if HW TX FIFO can take data now and no backlog exists, send immediately.
                if (!(uart->FR & FR_TXFF) && rb_empty(&txrb))
                {
                    uart->DR = c;
                }
                else
                {
                    if (!rb_write(&txrb, c))
                    {
                        _reply(returns.r0, 0, ZUART_ERR, 0);
                        break;
                    }
                    uart->IMSC |= IMSC_TXIM;
                }
                _reply(returns.r0, 0, ZUART_SEND_OK, 0);
            }
            break;
            case ZUART_CMD_READ:
            {
                if (!rb_empty(&rxrb))
                    _reply(returns.r0, rb_read(&rxrb), 0, 0);
                else if (!waitq_push(returns.r0))
                    _reply(returns.r0, 0, ZUART_ERR, 0);
            }
            break;
            }
        }
    }
}

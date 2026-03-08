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

static inline int waitq_empty(void)
{
    return read_waitq_head == read_waitq_tail;
}

static inline int waitq_full(void)
{
    return (uint16_t)((read_waitq_head + 1) % ZUART_WAITQ_MAX) == read_waitq_tail;
}

static inline int waitq_push(int32_t pid, int32_t handle, size_t len)
{
    if (waitq_full())
        return 0;
    read_waitq[read_waitq_head].pid = pid;
    read_waitq[read_waitq_head].shmem_handle = handle; // Note: consider changing this to int32_t in the header
    read_waitq[read_waitq_head].length = len;
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
        
        char *buf = (char *)_attach(waiter.shmem_handle);
        if ((intptr_t)buf >= 0) 
        {
            size_t read_count = 0;
            // Drain rxrb up to the waiter's requested length
            while (!rb_empty(&rxrb) && read_count < waiter.length) {
                buf[read_count++] = rb_read(&rxrb);
            }
            _memunmap(buf, 4096);
            _reply(waiter.pid, read_count, 0, 0);
        }
        else
        {
            // Attachment failed, tell the process it errored out
            _reply(waiter.pid, 0, ZUART_ERR, 0);
        }
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
                    char c = uart->DR;
                    rb_write(&rxrb, c); // write into rx ringbuf
                    uart->DR = c;       // echo it back immediately
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
            switch (returns.r1)
            {
            case ZUART_CMD_WRITE:
            {
                int32_t handle = (int32_t)returns.r2;
                size_t len = returns.r3;

                // attach to shmem
                char *buf = (char *)_attach(handle);
                if ((intptr_t)buf < 0) // Basic error check
                {
                    _reply(returns.r0, 0, ZUART_ERR, 0);
                    break;
                }

                // loop & write to txrb until full or done, then enable TX interrupt
                size_t written = 0;
                for (size_t i = 0; i < len; i++)
                {
                    char c = buf[i];
                    if (!(uart->FR & FR_TXFF) && rb_empty(&txrb))
                    {
                        uart->DR = c;
                    }
                    else
                    {
                        if (!rb_write(&txrb, c))
                            break; // tx buffer is full, stop copying early
                        uart->IMSC |= IMSC_TXIM;
                    }
                    written++;
                }

                // unmap shmem (assume 4kb)
                _memunmap(buf, 4096);

                // reply with number of bytes written (or error)
                _reply(returns.r0, written, ZUART_SEND_OK, 0);
            }
            break;
            case ZUART_CMD_READ:
            {
                int32_t handle = (int32_t)returns.r2;
                size_t requested_len = returns.r3;

                if (!rb_empty(&rxrb))
                {
                    char *buf = (char *)_attach(handle);
                    if ((intptr_t)buf < 0)
                    {
                        _reply(returns.r0, 0, ZUART_ERR, 0);
                        break;
                    }

                    size_t read_count = 0;
                    while (!rb_empty(&rxrb) && read_count < requested_len)
                    {
                        buf[read_count++] = rb_read(&rxrb);
                    }

                    _memunmap(buf, 4096);
                    _reply(returns.r0, read_count, 0, 0);
                }
                else
                {
                    // Push full context to wait queue
                    if (!waitq_push(returns.r0, handle, requested_len))
                        _reply(returns.r0, 0, ZUART_ERR, 0); // Queue full
                }
            }
            break;
            }
        }
    }
}

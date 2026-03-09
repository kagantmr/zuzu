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

// -------------------- TX helper --------------------
// Safely transmit one byte: write directly to hardware if the TX FIFO has
// room and txrb is drained, otherwise buffer it and arm the TX interrupt.
static void uart_txbyte(char c)
{
    if (!(uart->FR & FR_TXFF) && rb_empty(&txrb)) {
        uart->DR = c;
    } else if (rb_write(&txrb, c)) {
        uart->IMSC |= IMSC_TXIM;
    }
    // if txrb is also full the byte is silently dropped
}

// ---------------------------------------------------

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

        if (waiter.shmem_handle != cached_handle)
        {
            cached_buf    = (char *)_attach(waiter.shmem_handle);
            cached_handle = waiter.shmem_handle;
        }
        char *buf = cached_buf;
        if ((intptr_t)buf >= 0)
        {
            size_t n = 0;
            while (!rb_empty(&rxrb) && n < waiter.length)
                buf[n++] = rb_read(&rxrb);
            _reply(waiter.pid, n, 0, 0);
        }
        else
        {
            // Attachment failed; invalidate cache and report error
            cached_handle = -1;
            _reply(waiter.pid, 0, ZUART_ERR, 0);
        }
    }
}

int zuart_setup(void)
{
    uart = (volatile pl011_t *)_mapdev(VEXPRESS_UART0_PA, 0x1000);
    if (!uart)
        return ZUART_INIT_FAIL;

    if (_irq_claim(UART0_IRQ_NUM))
        return ZUART_INIT_FAIL;
    port = _port_create();
    if (port < 0)
        return ZUART_INIT_FAIL;
    if (_irq_bind(UART0_IRQ_NUM, port))
        return ZUART_INIT_FAIL;

    int32_t slot = _port_grant(port, NAMETABLE_PID);
    _call(NT_PORT, NT_REGISTER, nt_pack("uart"), slot);

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

        if (msg.r0 == 0)
        {
            // IRQ notification
            if (uart->MIS & IMSC_RXIM)
            {
                // Drain hardware RX FIFO into rxrb.
                // Only echo printable ASCII so control characters (CR, LF,
                // backspace/DEL) don't fight with the shell's own display logic.
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
        else if (msg.r0 > 0)
        {
            switch (msg.r1)
            {
            case ZUART_CMD_WRITE:
            {
                int32_t handle = (int32_t)msg.r2;
                size_t  len    = msg.r3;

                if (handle != cached_handle)
                {
                    cached_buf    = (char *)_attach(handle);
                    cached_handle = handle;
                }
                if ((intptr_t)cached_buf < 0)
                {
                    cached_handle = -1;
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
                int32_t handle       = (int32_t)msg.r2;
                size_t  requested    = msg.r3;

                if (!rb_empty(&rxrb))
                {
                    if (handle != cached_handle)
                    {
                        cached_buf    = (char *)_attach(handle);
                        cached_handle = handle;
                    }
                    if ((intptr_t)cached_buf < 0)
                    {
                        cached_handle = -1;
                        _reply(msg.r0, 0, ZUART_ERR, 0);
                        break;
                    }

                    size_t n = 0;
                    while (!rb_empty(&rxrb) && n < requested)
                        cached_buf[n++] = rb_read(&rxrb);
                    _reply(msg.r0, n, 0, 0);
                }
                else
                {
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
    }
}

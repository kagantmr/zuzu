#include "zuart.h"
#include "zuart_protocol.h"
#include "nt_protocol.h"
#include <stdint.h>

volatile pl011_t *uart;
int port;
ringbuf_t rxrb, txrb;

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
                else
                    _reply(returns.r0, 0, ZUART_ERR, 0);
            }
            break;
            }
        }
    }
}

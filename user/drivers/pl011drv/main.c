#include "pl011drv.h"
#include "zuzu/protocols/uart.h"
#include "zuzu/protocols/devm.h"
#include "zuzu/protocols/nametable.h"
#include <zuzu/protocols/exec.h>
#include "zuzu/lmsg.h"
#include "zuzu/service.h"
#include <ring.h>
#include <zuzu/cap.h>
#include <zuzu/channel.h>
#include <stdint.h>
#include <string.h>

#ifdef ZUZU_BENCH
#include <arch/cycles.h>
#include <snprintf.h>
#include <zuzu/bench.h>
#endif

#define PL011DRV_DEV_CLASS DEV_CLASS_SERIAL
#define PL011DRV_COMPATIBLE "arm,pl011"
/* rpi4's DTB lists "arm,pl011-axi" as the UART's *first* compatible string
 * (compatible = "arm,pl011-axi", "arm,pl011", "arm,primecell";), and the
 * kernel's DTB enumeration only keeps that first string per device (see
 * dtb_enum_devices() in kernel/dtb/dtb.c) -- so devmgr's exact strcmp
 * against just "arm,pl011" never matches on rpi4. Mirror the alias list
 * arch/arm/rpi4/platform.c already uses for the early console lookup. */
#define PL011DRV_COMPATIBLE_AXI "arm,pl011-axi"

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

static void wait_for_devmgr(void)
{
    while (1) {
        Handle ntmsg = LookupService("/svc/devmgr");
        if (ntmsg > 0) {
            devmgr_port = (int32_t)ntmsg;
            return;
        }
        ZuzuSleep(10);
    }
}

static Handle request_serial_device(void)
{
    static const char *const compat[] = { PL011DRV_COMPATIBLE, PL011DRV_COMPATIBLE_AXI };
    return DevmRequestDevice(devmgr_port, compat, 2, NULL);
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
    ZuzuIrqDone((uint32_t)serial_dev_handle);
}

/* ZuzuMsgLsend(client_port, len): fire-and-forget write, payload in lmsg_buf(). */
static void handle_write(uint32_t len)
{
    if (len > LMSG_BUF_SIZE)
        len = LMSG_BUF_SIZE;

    const char *buf = LmsgBuf();
    for (uint32_t i = 0; i < len; i++)
        uart_txbyte(buf[i]);
}

/* ZuzuMsgLcall(client_port, max_len): read up to max_len bytes already
 * buffered from the UART; replies immediately with however many are
 * available (possibly zero) rather than blocking for more. */
static void handle_read(Handle reply_handle, uint32_t max_len)
{
    if (max_len > LMSG_BUF_SIZE)
        max_len = LMSG_BUF_SIZE;

    drain_uart_rx_fifo();

    char *buf = (char *)LmsgBuf();
    uint32_t n = 0;
    while (n < max_len && ring_avail(&rxrb) > 0) {
        uint8_t b = 0;
        if (ring_pop(&rxrb, &b) != 0)
            break;
        buf[n++] = (char)b;
    }

    (void)ChannelReply(reply_handle, buf, n);
}

#ifdef ZUZU_BENCH
static void uart_bench_print(const char *label, const BenchResult *r)
{
    uint64_t avg_x100 = r->count ? (r->sum * 100) / r->count : 0;
    char line[96];
    int n = snprintf(line, sizeof(line),
                      "[BENCH] %-32s min=%-8u avg=%u.%02u max=%-8u (cycles, n=%u)\n", label,
                      r->min, (uint32_t)(avg_x100 / 100), (uint32_t)(avg_x100 % 100), r->max,
                      r->count);
    if (n < 0)
        return;
    if ((size_t)n >= sizeof(line))
        n = (int)sizeof(line) - 1;
    for (int i = 0; i < n; i++)
        uart_txbyte(line[i]);
}

/* Exercises the kernel's IRQ-wait block->unblock bracket (SysNtfnWait's
 * block point / relay_handler's unblock point, kernel/irq/sys_irq.c) using
 * our own TX-empty interrupt as a real, self-triggerable hardware IRQ
 * source. No bytes ever go on the wire: the FIFO is already empty, so
 * unmasking IMSC_TXIM alone makes the PL011 assert the interrupt. Run
 * once, before this driver takes client traffic. */
static void run_irq_wait_bench(void)
{
    BenchResult r = { 0 };
    uint32_t total = ZUZU_BENCH_WARMUP_ITERS + ZUZU_BENCH_ITERS;

    for (uint32_t i = 0; i < total; i++) {
        uart->ICR = IMSC_TXIM;

        uint32_t start = ArchMeasure();
        uart->IMSC |= IMSC_TXIM;

        (void)ZuzuNtfnWait(serial_irq_ntfn, TIMEOUT_INFINITE);
        uint32_t end = ArchMeasure();

        uart->IMSC &= ~IMSC_TXIM;
        uart->ICR = IMSC_TXIM;
        ZuzuIrqDone((uint32_t)serial_dev_handle);

        if (i >= ZUZU_BENCH_WARMUP_ITERS)
            bench_result_record(&r, end - start);
    }

    uart_bench_print("IRQ wait block->unblock", &r);
}
#endif /* ZUZU_BENCH */

int pl011drv_setup(void)
{
    client_port = ZuzuPortCreate();
    if (client_port < 0) {
        return client_port;
    }

    Err rc = RegisterService("/dev/uart0", client_port);
    if (rc < 0) {
        return rc;
    }

    wait_for_devmgr();

    int32_t dev_handle = request_serial_device();
    if (dev_handle < 0) {
        return dev_handle;
    }

    serial_irq_ntfn = ZuzuNtfnCreate();
    if (serial_irq_ntfn < 0) {
        return serial_irq_ntfn;
    }

    int32_t bind_rc = ZuzuIrqBind(dev_handle, (uint32_t)serial_irq_ntfn);
    if (bind_rc < 0) {
        return bind_rc;
    }

    serial_dev_handle = dev_handle;
    uart = (volatile pl011_t *)ZuzuMemMap(dev_handle, 0, PROT_RW, 0);
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

#ifdef ZUZU_BENCH
    run_irq_wait_bench();
#endif

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
        if (ZuzuWaitany(handles, 2, TIMEOUT_INFINITE, &r) != 0)
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

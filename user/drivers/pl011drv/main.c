#include "pl011drv.h"
#include <dev/protocols/devm.h>
#include <dev/protocols/tty.h>
#include <string.h>
#include <util/msg.h>
#include <util/shm_ring.h>
#include <zuzu/err.h>
#include <zuzu/service.h>
#include <zuzu/udbg.h>
#include <zuzu/zuzu.h>

#define PL011DRV_COMPATIBLE "arm,pl011"

#define PL011DRV_COMPATIBLE_AXI "arm,pl011-axi"

#define BIT_IRQ 0
#define BIT_KICK 1 /* ttysvc rings this on our event */
#define POLL_MS 10 /* safety net  */
#define MASK(bit) (1U << (bit))

static volatile Pl011Mmio *uart;
static Handle devsvc_port = -1;
static Handle g_dev = -1;
static Handle g_event = -1;
static TtyConn g_tty;
static bool g_online;

static bool UartTxPump(void)
{
    if (!g_online)
        return false;
    TtyShm *shm = g_tty.shm;
    bool popped = false;
    while (!(uart->fr & FR_TXFF))
    {
        uint8_t b = 0;
        if (ShmRingPop(&shm->down_hdr, shm->down_data, &b, 1) == 0)
        {
            uart->imsc &= ~IMSC_TXIM;
            return popped;
        }
        uart->dr = b;
        popped = true;
    }
    uart->imsc |= IMSC_TXIM;
    return popped;
}

static bool UartRxPump(void)
{
    bool pushed_any = false;
    while (!(uart->fr & FR_RXFE))
    {
        if (g_online && ShmRingFree(&g_tty.shm->up_hdr) == 0)
        {
            uart->imsc &= ~(IMSC_RXIM | IMSC_RTIM);
            return pushed_any;
        }
        uint32_t dr = uart->dr;
        if (dr & 0xF00U)
        {
            uart->rsr = 0xFU;
            continue;
        }
        if (!g_online)
            continue;
        uint8_t b = (uint8_t)(dr & 0xFFU);
        ShmRingPush(&g_tty.shm->up_hdr, g_tty.shm->up_data, &b, 1);
        pushed_any = true;
    }
    uart->imsc |= (IMSC_RXIM | IMSC_RTIM);
    return pushed_any;
}

static Handle WaitForTty(void)
{
    for (;;)
    {
        Handle h = LookupService("/svc/tty");
        if (h >= 0)
            return h;
        Sleep(10);
    }
}

static Err WaitForDevsvc(void)
{
    for (;;)
    {
        Handle h = LookupService("/svc/devsvc");
        if (h >= 0)
        {
            devsvc_port = h;
            return ZUZU_OK;
        }
        Sleep(10);
    }
}

static Handle RequestSerialDevice(void)
{
    static const char *const compat[] = {PL011DRV_COMPATIBLE, PL011DRV_COMPATIBLE_AXI};
    return RequestDevice(devsvc_port, compat, 2, NULL);
}

static Err Pl011DrvSetup(void)
{
    Err rc = WaitForDevsvc();
    UserspaceDebugLog("pl011drv: devsvc lookup rc=%d", rc);
    if (rc != ZUZU_OK)
        return rc;

    g_dev = RequestSerialDevice();
    UserspaceDebugLog("pl011drv: device handle=%d", g_dev);
    if (g_dev < 0)
        return (Err)g_dev;

    Pl011Mmio *mmio = MemMap(g_dev, 0, PROT_RW);
    UserspaceDebugLog("pl011drv: mmio=0x%x", (unsigned)mmio);
    if (PtrIsErr(mmio))
        return (Err)mmio;
    uart = (volatile Pl011Mmio *)mmio;

    uart->imsc = 0;
    uart->cr = 0;
    uart->icr = ICR_ALL;
    uart->ifls = (uart->ifls & ~IFLS_RX_MASK) | IFLS_RX_1_8;
    uart->lcrh = LCRH_FEN | LCRH_WLEN_8;
    uart->cr = CR_UARTEN | CR_TXE | CR_RXE;
    uart->icr = ICR_ALL;

    g_event = CreateEvent();
    if (g_event < 0)
        return (Err)g_event;
    rc = BindIrq(g_event, g_dev, BIT_IRQ);
    UserspaceDebugLog("pl011drv: irq bind rc=%d", rc);
    if (rc != ZUZU_OK)
        return rc;

    Handle tty_port = WaitForTty();
    rc = TtyClientConnect(tty_port, TTY_PROVIDE, "uart0", g_event, BIT_KICK, &g_tty);
    UserspaceDebugLog("pl011drv: provide uart0 rc=%d", rc);
    if (rc != ZUZU_OK)
        return rc;
    g_online = true;

    uart->imsc = (IMSC_RXIM | IMSC_RTIM);
    return ZUZU_OK;
}

static void KickTty(void)
{
    Err rc = Signal(g_tty.doorbell, MASK(g_tty.bit), false);
    if (rc != ZUZU_OK)
        UserspaceDebugLog("pl011drv: kick ttysvc failed rc=%d", rc);
}

int main(void)
{
    UserspaceDebugLog("pl011drv: started");
    Err rc = Pl011DrvSetup();
    if (rc != ZUZU_OK)
        return rc;
    UserspaceDebugLog("pl011drv: entering event loop");

    for (;;)
    {
        EventWaitResult ev = FormatToEventWait(WaitOn(g_event, POLL_MS));
        bool irq = ev.status == ZUZU_OK && (ev.bits & MASK(BIT_IRQ));

        bool rx = false;
        bool tx = false;
        for (;;)
        {
            uart->icr = ICR_ALL;
            rx |= UartRxPump();
            tx |= UartTxPump();
            if (irq)
            {
                IrqRearm(g_dev);
                irq = false;
            }
            /* Re-check after unmasking: a byte that arrived while the line was
             * masked may not raise a new interrupt. Stop if the FIFO is empty,
             * or if it isn't only because ttysvc's ring is full. */
            if ((uart->fr & FR_RXFE) || ShmRingFree(&g_tty.shm->up_hdr) == 0)
                break;
        }
        if (rx || tx)
            KickTty();
    }
}

#include "pl011drv.h"
#include <dev/protocols/devm.h>
#include <dev/protocols/uart.h>
#include <string.h>
#include <util/msg.h>
#include <util/shm_ring.h>
#include <zuzu/err.h>
#include <zuzu/service.h>
#include <zuzu/udbg.h>
#include <zuzu/zuzu.h>

#define PL011DRV_COMPATIBLE "arm,pl011"
/* rpi4's DTB lists "arm,pl011-axi" as the UART's *first* compatible string;
 * the kernel's DTB enumeration only keeps that first string, so devsvc's
 * exact match against "arm,pl011" never fires on rpi4. */
#define PL011DRV_COMPATIBLE_AXI "arm,pl011-axi"

#define BIT_IRQ 0
#define BIT_PORT 1
#define BIT_KICK 2
#define MASK(bit) (1U << (bit))

static volatile Pl011Mmio *uart;
static Handle devsvc_port = -1;
static Handle client_port = -1;
static Handle g_dev = -1;
static Handle g_event = -1;
static Handle g_doorbell = -1;

typedef struct
{
    bool in_use;
    UartShm *shm;
    Handle mem;
    Handle client_doorbell;
    uint32_t client_bit;
} Session;

static Session g_session = { .mem = -1, .client_doorbell = -1 };

static void ReplyStatus(Err status)
{
    memcpy(MessageBuf(), &status, sizeof(status));
    Reply(sizeof(status), -1);
}

static void UartTxPump(void)
{
    if (!g_session.in_use)
        return;
    UartShm *shm = g_session.shm;
    while (!(uart->fr & FR_TXFF))
    {
        uint8_t b = 0;
        if (ShmRingPop(&shm->tx_hdr, shm->tx_data, &b, 1) == 0)
        {
            uart->imsc &= ~IMSC_TXIM;
            return;
        }
        uart->dr = b;
    }
    uart->imsc |= IMSC_TXIM;
}

/* RX interrupts are level-triggered on FIFO occupancy, so the FIFO must be
 * emptied every time: with no session the bytes are dropped, and if the
 * reader's ring is full RX interrupts are masked until it kicks us. */
static bool UartRxPump(void)
{
    bool pushed_any = false;
    while (!(uart->fr & FR_RXFE))
    {
        if (g_session.in_use && ShmRingFree(&g_session.shm->rx_hdr) == 0)
        {
            uart->imsc &= ~(IMSC_RXIM | IMSC_RTIM);
            return pushed_any;
        }
        uint32_t dr = uart->dr;
        if (dr & 0xF00u)
        {
            uart->rsr = 0xFu;
            continue;
        }
        if (!g_session.in_use)
            continue;
        uint8_t b = (uint8_t)(dr & 0xFFu);
        ShmRingPush(&g_session.shm->rx_hdr, g_session.shm->rx_data, &b, 1);
        pushed_any = true;
    }
    uart->imsc |= (IMSC_RXIM | IMSC_RTIM);
    return pushed_any;
}

static void UartHandleOpen(Handle granted_mem)
{
    if (granted_mem < 0)
    {
        ReplyStatus(ERR_BADARG);
        return;
    }
    if (g_session.in_use)
    {
        HandleClose(granted_mem);
        ReplyStatus(ERR_BUSY);
        return;
    }

    VirtAddr va = MemMap(granted_mem, 0, PROT_RW);
    if (PtrIsErr((void *)va))
    {
        HandleClose(granted_mem);
        ReplyStatus((Err)va);
        return;
    }

    g_session.shm = (UartShm *)va;
    g_session.mem = granted_mem;
    ShmRingInit(&g_session.shm->tx_hdr, UART_RING_DATA_SIZE);
    ShmRingInit(&g_session.shm->rx_hdr, UART_RING_DATA_SIZE);
    g_session.in_use = true;

    UartOpenReply rep = { .status = ZUZU_OK, .bit = BIT_KICK };
    memcpy(MessageBuf(), &rep, sizeof(rep));
    if (Reply(sizeof(rep), g_doorbell) != ZUZU_OK)
    {
        MemUnmap(va);
        HandleClose(granted_mem);
        g_session = (Session){ .mem = -1, .client_doorbell = -1 };
    }
}

static void UartHandleNotify(const UartNotifyRequest *req, Handle granted)
{
    if (!g_session.in_use || granted < 0 || req->bit >= 31)
    {
        if (granted >= 0)
            HandleClose(granted);
        ReplyStatus(ERR_BADARG);
        return;
    }
    if (g_session.client_doorbell >= 0)
        HandleClose(g_session.client_doorbell);
    g_session.client_doorbell = granted;
    g_session.client_bit = req->bit;
    ReplyStatus(ZUZU_OK);
}


static void HandleRequest(const PortWaitResult *r)
{
    uint32_t cmd;
    if (r->xlen >= sizeof(cmd))
    {
        memcpy(&cmd, MessageBuf(), sizeof(cmd));
        switch (cmd)
        {
        case UART_OPEN:
            UartHandleOpen(r->granted);
            return;
        case UART_NOTIFY:
            if (r->xlen >= sizeof(UartNotifyRequest))
            {
                UartNotifyRequest req;
                memcpy(&req, MessageBuf(), sizeof(req));
                UartHandleNotify(&req, r->granted);
                return;
            }
            break;
        }
    }
    if (r->granted >= 0)
        HandleClose(r->granted);
    ReplyStatus(ERR_BADARG);
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
    static const char *const compat[] = { PL011DRV_COMPATIBLE, PL011DRV_COMPATIBLE_AXI };
    return RequestDevice(devsvc_port, compat, 2, NULL);
}

static Err Pl011DrvSetup(void)
{
    client_port = CreatePort();
    if (client_port < 0)
        return (Err)client_port;
    UserspaceDebugLog("pl011drv: client port=%d", client_port);

    Err rc = RegisterService("/dev/uart0", client_port);
    UserspaceDebugLog("pl011drv: register /dev/uart0 rc=%d", rc);
    if (rc != ZUZU_OK)
        return rc;

    rc = WaitForDevsvc();
    UserspaceDebugLog("pl011drv: devsvc lookup rc=%d", rc);
    if (rc != ZUZU_OK)
        return rc;

    g_dev = RequestSerialDevice();
    UserspaceDebugLog("pl011drv: device handle=%d", g_dev);
    if (g_dev < 0)
        return (Err)g_dev;

    VirtAddr mmio = MemMap(g_dev, 0, PROT_RW);
    UserspaceDebugLog("pl011drv: mmio=0x%x", (unsigned)mmio);
    if (PtrIsErr((void *)mmio))
        return (Err)mmio;
    uart = (volatile Pl011Mmio *)mmio;

    uart->imsc = 0;
    uart->cr = 0;
    uart->icr = ICR_ALL;
    uart->ifls = (uart->ifls & ~IFLS_RX_MASK) | IFLS_RX_1_8;
    uart->lcrh = LCRH_WLEN_8;          /* was: LCRH_FEN | LCRH_WLEN_8 */
    uart->cr = CR_UARTEN | CR_TXE | CR_RXE;
    uart->icr = ICR_ALL;

    g_event = CreateEvent();
    if (g_event < 0)
        return (Err)g_event;


    SvcResult dup = HandleDuplicate(g_event, PERM_SEND | PERM_TXFR, MARKER_NONE);
    if (dup.r0 != ZUZU_OK)
        return (Err)dup.r0;
    g_doorbell = (Handle)dup.r1;

    rc = BindIrq(g_event, g_dev, BIT_IRQ);
    if (rc != ZUZU_OK)
        return rc;
    rc = Bind(EVENT_PORT, g_event, client_port, BIT_PORT);
    UserspaceDebugLog("pl011drv: irq/port bind rc=%d", rc);
    if (rc != ZUZU_OK)
        return rc;

    uart->imsc = (IMSC_RXIM | IMSC_RTIM);
    return ZUZU_OK;
}

int main(void)
{
    UserspaceDebugLog("pl011drv: started");
    Err rc = Pl011DrvSetup();
    UserspaceDebugLog("pl011drv: setup rc=%d", rc);
    if (rc != ZUZU_OK)
        return rc;
    UserspaceDebugLog("pl011drv: entering event loop");

    for (;;)
    {
        EventWaitResult ev = FormatToEventWait(WaitOn(g_event, TIMEOUT_INFINITE));
        if (ev.status != ZUZU_OK)
            continue;

       // UserspaceDebugLog("pl011drv: wake bits=%x fr=%x", ev.bits, uart->fr);
        if (ev.bits & MASK(BIT_PORT))
        {
            for (;;)
            {
                PortWaitResult r = FormatToPortWait(WaitOn(client_port, TIMEOUT_POLL));
                if (r.status != ZUZU_OK)
                    break;
                HandleRequest(&r);
            }
        }

        bool rx = false;
        bool irq = ev.bits & MASK(BIT_IRQ);
        for (;;)
        {
            uart->icr = ICR_ALL;
            rx |= UartRxPump();
            UartTxPump();
            if (irq)
            {
                IrqRearm(g_dev);
                irq = false;
            }
            /* Re-check after unmasking: a byte that arrived while the line was
             * masked may not raise a new interrupt. Stop if the FIFO is empty,
             * or if it isn't only because the reader's ring is full. */
            if ((uart->fr & FR_RXFE) ||
                (g_session.in_use && ShmRingFree(&g_session.shm->rx_hdr) == 0))
                break;
        }
        if (rx && g_session.client_doorbell >= 0)
            Signal(g_session.client_doorbell, MASK(g_session.client_bit), false);
    }
}
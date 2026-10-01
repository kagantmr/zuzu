#include "pl011drv.h"
#include <dev/protocols/devm.h>
#include <dev/protocols/uart.h>
#include <string.h>
#include <util/msg.h>
#include <util/shm_ring.h>
#include <zuzu/err.h>
#include <zuzu/service.h>
#include <zuzu/zuzu.h>

#define PL011DRV_COMPATIBLE "arm,pl011"
/* rpi4's DTB lists "arm,pl011-axi" as the UART's *first* compatible string;
 * the kernel's DTB enumeration only keeps that first string, so devsvc's
 * exact match against "arm,pl011" never fires on rpi4. */
#define PL011DRV_COMPATIBLE_AXI "arm,pl011-axi"
#define STACK_SIZE (16 * 1024)

static volatile Pl011Mmio *uart;
static Handle devmgr_port = -1;
static Handle client_port = -1;
static Handle irq_event = -1;
static Handle session_event = -1;

typedef struct
{
    bool in_use;
    UartShm *shm;
    Handle mem;
    Handle task;
} Session;

static Session g_session;

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
        uint8_t b;
        if (ShmRingPop(&shm->tx_hdr, shm->tx_data, &b, 1) == 0)
        {
            uart->imsc &= ~IMSC_TXIM;
            return;
        }
        uart->dr = b;
    }
    uart->imsc |= IMSC_TXIM;
}

/* Drains whatever's in the hardware FIFO into the RX ring. If the ring
 * fills up first, stops and leaves the rest in hardware -- natural
 * backpressure; a slow reader risks a hardware overrun, not a kernel one. */
static bool UartRxPump(void)
{
    if (!g_session.in_use)
        return false;
    UartShm *shm = g_session.shm;
    bool pushed_any = false;
    while (!(uart->fr & FR_RXFE))
    {
        uint32_t dr = uart->dr;
        if (dr & 0xF00u)
        {
            uart->rsr = 0xFu;
            continue;
        }
        uint8_t b = (uint8_t)(dr & 0xFFu);
        if (ShmRingPush(&shm->rx_hdr, shm->rx_data, &b, 1) == 0)
            break;
        pushed_any = true;
    }
    return pushed_any;
}

static void TeardownSession(void)
{
    MemUnmap((VirtAddr)g_session.shm);
    HandleClose(g_session.mem);
    HandleClose(g_session.task);
    g_session.in_use = false;
    g_session.shm = NULL;
    g_session.mem = -1;
    g_session.task = -1;
}

static void UartHandleOpen(Handle granted_mem)
{
    if (g_session.in_use || granted_mem < 0)
    {
        ReplyStatus(ERR_BUSY);
        return;
    }

    VirtAddr va = MemMap(granted_mem, 0, PROT_RW);
    if (PtrIsErr((void *)va))
    {
        ReplyStatus((Err)va);
        return;
    }

    g_session.shm = (UartShm *)va;
    g_session.mem = granted_mem;
    g_session.task = -1;
    ShmRingInit(&g_session.shm->tx_hdr, UART_RING_DATA_SIZE);
    ShmRingInit(&g_session.shm->rx_hdr, UART_RING_DATA_SIZE);
    g_session.in_use = true;

    UartOpenReply rep = { .status = ZUZU_OK, .bit = 0 };
    memcpy(MessageBuf(), &rep, sizeof(rep));
    Reply(sizeof(rep), session_event);
}

static void UartHandleBindTask(const UartBindTaskRequest *req, Handle granted_task)
{
    if (!g_session.in_use || req->bit != 0 || granted_task < 0)
    {
        ReplyStatus(ERR_BADARG);
        return;
    }
    g_session.task = granted_task;
    Bind(EVENT_TASK, session_event, granted_task, 0);
    ReplyStatus(ZUZU_OK);
}

static void UartAcceptLoop(void)
{
    for (;;)
    {
        PortWaitResult r = FormatToPortWait(WaitOn(client_port, TIMEOUT_INFINITE));
        if (r.status != ZUZU_OK)
            continue;
        if (r.xlen < 4)
        {
            ReplyStatus(ERR_BADARG);
            continue;
        }

        uint32_t cmd;
        memcpy(&cmd, MessageBuf(), 4);

        switch (cmd)
        {
        case UART_OPEN:
            UartHandleOpen(r.granted);
            break;
        case UART_BIND_TASK:
        {
            UartBindTaskRequest req;
            if (r.xlen < sizeof(req))
            {
                ReplyStatus(ERR_BADARG);
                break;
            }
            memcpy(&req, MessageBuf(), sizeof(req));
            UartHandleBindTask(&req, r.granted);
        }
        break;
        default:
            ReplyStatus(ERR_BADARG);
            break;
        }
    }
}

/* One shared bit does double duty: the client Signal()s it on new TX
 * data, and Bind(EVENT_TASK,...) signals it when the client dies. A
 * non-blocking poll on the task handle disambiguates. */
static void UartSessionLoop(void)
{
    for (;;)
    {
        EventWaitResult r = FormatToEventWait(WaitOn(session_event, TIMEOUT_INFINITE));
        if (r.status != ZUZU_OK || !g_session.in_use)
            continue;

        UartTxPump();

        TaskWaitResult tw = FormatToTaskWait(WaitOn(g_session.task, TIMEOUT_POLL));
        if (tw.status == ZUZU_OK)
            TeardownSession();
    }
}

static void UartIrqLoop(void)
{
    for (;;)
    {
        EventWaitResult r = FormatToEventWait(WaitOn(irq_event, TIMEOUT_INFINITE));
        if (r.status != ZUZU_OK)
            continue;

        bool rx_data = UartRxPump();
        uart->icr = ICR_ALL;
        UartTxPump();

        if (rx_data && g_session.in_use)
            Signal(session_event, 1U, true);
    }
}

static Handle SpawnThread(void (*entry)(void))
{
    VirtAddr stack = MemMapAnon(STACK_SIZE, 0, PROT_READ | PROT_WRITE);
    if (PtrIsErr((void *)stack))
        return (Handle)stack;

    Handle h = CreateTask(-1);
    if (h < 0)
        return h;

    Err rc = TaskStart(h, (VirtAddr)entry, stack + STACK_SIZE, 0, 0);
    if (rc != ZUZU_OK)
    {
        HandleClose(h);
        return rc;
    }
    return h;
}

static Err WaitForDevmgr(void)
{
    for (;;)
    {
        Handle h = LookupService("/svc/devsvc");
        if (h >= 0)
        {
            devmgr_port = h;
            return ZUZU_OK;
        }
        Sleep(10);
    }
}

static Handle RequestSerialDevice(void)
{
    static const char *const compat[] = { PL011DRV_COMPATIBLE, PL011DRV_COMPATIBLE_AXI };
    return RequestDevice(devmgr_port, compat, 2, NULL);
}

static Err Pl011DrvSetup(void)
{
    client_port = CreatePort();
    if (client_port < 0)
        return (Err)client_port;

    Err rc = RegisterService("/dev/uart0", client_port);
    if (rc != ZUZU_OK)
        return rc;

    rc = WaitForDevmgr();
    if (rc != ZUZU_OK)
        return rc;

    Handle dev_handle = RequestSerialDevice();
    if (dev_handle < 0)
        return (Err)dev_handle;

    irq_event = CreateEvent();
    if (irq_event < 0)
        return (Err)irq_event;
    rc = BindIrq(irq_event, dev_handle, 0);
    if (rc != ZUZU_OK)
        return rc;

    session_event = CreateEvent();
    if (session_event < 0)
        return (Err)session_event;

    VirtAddr mmio = MemMap(dev_handle, 0, PROT_RW);
    if (PtrIsErr((void *)mmio))
        return (Err)mmio;
    uart = (volatile Pl011Mmio *)mmio;

    uart->imsc = 0;
    uart->cr = 0;
    uart->icr = ICR_ALL;
    uart->ifls = (uart->ifls & ~IFLS_RX_MASK) | IFLS_RX_1_8;
    uart->lcrh = LCRH_FEN | LCRH_WLEN_8;
    uart->cr = CR_UARTEN | CR_TXE | CR_RXE;
    uart->icr = ICR_ALL;
    uart->imsc = (IMSC_RXIM | IMSC_RTIM); /* TXIM is toggled dynamically */

    g_session.task = -1;
    g_session.mem = -1;

    return ZUZU_OK;
}

int main(void)
{
    Err rc = Pl011DrvSetup();
    if (rc != ZUZU_OK)
        return rc;

    SpawnThread(UartIrqLoop);
    SpawnThread(UartSessionLoop);
    UartAcceptLoop();
    return 0;
}
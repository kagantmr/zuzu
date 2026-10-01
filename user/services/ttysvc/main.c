#include <dev/protocols/uart.h>
#include <string.h>
#include <util/msg.h>
#include <util/shm_ring.h>
#include <zuzu/err.h>
#include <zuzu/service.h>
#include <zuzu/udbg.h>
#include <zuzu/zuzu.h>

#define MY_BIT 0 /* bit the driver rings on our event */

static Handle g_event = -1;
static Handle g_driver_doorbell = -1;
static uint32_t g_driver_bit;
static UartShm *g_shm;

static Handle WaitForUart(void)
{
    for (;;)
    {
        Handle h = LookupService("/dev/uart0");
        if (h >= 0)
            return h;
        Sleep(10);
    }
}

/* Sends req (+ optional grant); the driver always replies with an Err in
 * the first 4 bytes of the payload. *granted gets the handle it granted
 * back (or -1). */
static Err DriverCall(Handle port, const void *req, uint32_t len, Handle grant, Handle *granted)
{
    MsgWrite(req, len);
    SvcResult r = Call(port, len, grant);
    if (granted)
        *granted = (Handle)r.r3;
    if (r.r0 != ZUZU_OK)
        return (Err)r.r0;
    Err status;
    if (r.r1 < sizeof(status))
        return ERR_BADARG;
    MsgRead(&status, sizeof(status));
    return status;
}

static Err OpenSession(Handle port)
{
    Handle mem = CreateMem(1);
    if (mem < 0)
        return (Err)mem;
    VirtAddr va = MemMap(mem, 0, PROT_RW);
    if (PtrIsErr((void *)va))
        return (Err)va;
    g_shm = (UartShm *)va;

    g_event = CreateEvent();
    if (g_event < 0)
        return (Err)g_event;

    /* Dup before granting: grants copy perms verbatim. */
    SvcResult page = HandleDuplicate(mem, PERM_MAP | PERM_TXFR, MARKER_NONE);
    if (page.r0 != ZUZU_OK)
        return (Err)page.r0;

    UartOpenRequest open = { .cmd = UART_OPEN };
    Handle doorbell;
    Err rc = DriverCall(port, &open, sizeof(open), (Handle)page.r1, &doorbell);
    HandleClose((Handle)page.r1);
    if (rc != ZUZU_OK)
        return rc;
    if (doorbell < 0)
        return ERR_BADARG;

    /* UartOpenReply is {status, bit}; DriverCall consumed status. */
    UartOpenReply rep;
    MsgRead(&rep, sizeof(rep));
    g_driver_doorbell = doorbell;
    g_driver_bit = rep.bit;

    SvcResult mine = HandleDuplicate(g_event, PERM_SEND | PERM_TXFR, MARKER_NONE);
    if (mine.r0 != ZUZU_OK)
        return (Err)mine.r0;

    UartNotifyRequest notify = { .cmd = UART_NOTIFY, .bit = MY_BIT };
    rc = DriverCall(port, &notify, sizeof(notify), (Handle)mine.r1, NULL);
    HandleClose((Handle)mine.r1);
    return rc;
}

int main(void)
{
    Handle port = WaitForUart();
    UserspaceDebugLog("ttysvc: found /dev/uart0");

    Err rc = OpenSession(port);
    if (rc != ZUZU_OK)
    {
        UserspaceDebugLog("ttysvc: open session failed rc=%d", rc);
        return rc;
    }
    UserspaceDebugLog("ttysvc: session open, echoing");

    for (;;)
    {
        EventWaitResult ev = FormatToEventWait(WaitOn(g_event, TIMEOUT_INFINITE));
        if (ev.status != ZUZU_OK)
            continue;

        /* Bits are hints: just drain RX, whatever woke us. */
        bool popped = false, pushed = false;
        uint8_t b;
        while (ShmRingFree(&g_shm->tx_hdr) >= 2 &&
               ShmRingPop(&g_shm->rx_hdr, g_shm->rx_data, &b, 1) == 1)
        {
            popped = true;
            pushed = true;
            ShmRingPush(&g_shm->tx_hdr, g_shm->tx_data, &b, 1);
            if (b == '\r')
            {
                uint8_t nl = '\n';
                ShmRingPush(&g_shm->tx_hdr, g_shm->tx_data, &nl, 1);
            }
        }

        /* Kick after pushing TX *and* after draining RX: the driver masks
         * RX interrupts when our ring fills and only unmasks on a kick. */
        if (popped || pushed)
            Signal(g_driver_doorbell, 1U << g_driver_bit, false);
    }
}
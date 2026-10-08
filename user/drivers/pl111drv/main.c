#include "pl111drv.h"
#include <types.h>
#include <zuzu/service.h>
#include <zuzu/zuzu.h>
#include <dev/protocols/devm.h>

static Handle g_device_handle = -1;
static Pl111Mmio *pl111;

Err DevsvcHandshake(void)
{

    Handle devsvc_h = -1;
    for (;;) {
        devsvc_h = LookupService("/svc/devsvc");
        if (devsvc_h >= 0)
            break;
        Sleep(10);
    }

    static const char *compat[] = {"arm,pl111"};
    g_device_handle = RequestDevice(devsvc_h, compat, 1, NULL);
    if (g_device_handle < 0)
        return g_device_handle;

    pl111 = MemMap(g_device_handle, 0, PROT_RW);
    if (PtrIsErr(pl111)) {
        return (Err)pl111;
    }
    return ZUZU_OK;
}

int main(void)
{
    Err retval;

    retval = DevsvcHandshake();
    if (retval < 0)
        return retval;
    return 0;
}

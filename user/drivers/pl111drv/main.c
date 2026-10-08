#include "pl111drv.h"
#include <types.h>
#include <zuzu/service.h>
#include <zuzu/zuzu.h>
#include <dev/protocols/devm.h>

#define LOG_TAG "pl111drv"
#include <util/log.h>


static Handle g_device_handle = -1;
static Pl111Mmio *pl111;
static Handle g_fb_handle = -1;
static void *g_fb;

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
    if (g_device_handle < 0) {
        LOG_ERROR(LOG_TAG, "Could not find compat");
        return g_device_handle;
    }

    pl111 = MemMap(g_device_handle, 0, PROT_RW);
    if (PtrIsErr(pl111)) {
        LOG_ERROR(LOG_TAG, "Could not map device MMIO");
        return (Err)pl111;
    }
    return ZUZU_OK;
}

Err Pl111Setup(void) {
    uint32_t part = (pl111->periph_id[0] & 0xFF) | ((pl111->periph_id[1] & 0xF) << 8);
    uint32_t magic = (pl111->pcell_id[0] & 0xFF) | ((pl111->pcell_id[1] & 0xFF) << 8) |
                     ((pl111->pcell_id[2] & 0xFF) << 16) | ((uint32_t)(pl111->pcell_id[3] & 0xFF) << 24);
    if (magic != 0xB105F00D || part != 0x111) {
        return ERR_SYSDOWN;
    }

    g_fb_handle = CreateMem(150, MEM_CONTIG | MEM_UNCACHED);
    if (g_fb_handle < 0)
        return g_fb_handle;
    g_fb = MemMap(g_fb_handle, 0, PROT_RW);
    if (PtrIsErr(g_fb))
        return (Err)g_fb;

    DmaMapResult res = DmaMap(g_device_handle, g_fb_handle, 0, (150 * 4096), DMA_TO_DEVICE);
    LOG_INFO(LOG_TAG, "DmaMap: status %d bus_address %x", res.status, res.bus_addr);

    return ZUZU_OK;
}

int main(void)
{
    Err retval;

    retval = DevsvcHandshake();
    if (retval < 0)
        return retval;

    retval = Pl111Setup();
    if (retval < 0)
        return retval;
    
    return 0;
}

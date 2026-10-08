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

static void DrawColorBars(volatile uint16_t *fb)
{
    static const uint16_t bars[8] = {0xFFFF, 0xFFE0, 0x07FF, 0x07E0,
                                     0xF81F, 0xF800, 0x001F, 0x0000};
    const uint32_t bar_width = FB_WIDTH / 8U;

    for (uint32_t y = 0; y < FB_HEIGHT; y++)
        for (uint32_t x = 0; x < FB_WIDTH; x++)
            fb[(y * FB_WIDTH) + x] = bars[x / bar_width];
}

Err Pl111Setup(void)
{
    uint32_t part = (pl111->periph_id[0] & 0xFF) | ((pl111->periph_id[1] & 0xF) << 8);
    uint32_t magic = (pl111->pcell_id[0] & 0xFF) | ((pl111->pcell_id[1] & 0xFF) << 8) |
                     ((pl111->pcell_id[2] & 0xFF) << 16) |
                     ((uint32_t)(pl111->pcell_id[3] & 0xFF) << 24);
    if (magic != 0xB105F00D || part != 0x111) {
        LOG_ERROR(LOG_TAG, "not a PL111: part %x magic %x", part, magic);
        return ERR_SYSDOWN;
    }

    g_fb_handle = CreateMem(FB_PAGES, MEM_CONTIG | MEM_UNCACHED);
    if (g_fb_handle < 0) {
        LOG_ERROR(LOG_TAG, "framebuffer alloc failed: %d", g_fb_handle);
        return g_fb_handle;
    }
    g_fb = MemMap(g_fb_handle, 0, PROT_RW);
    if (PtrIsErr(g_fb)) {
        LOG_ERROR(LOG_TAG, "framebuffer map failed");
        return (Err)g_fb;
    }

    DmaMapResult res = DmaMap(g_device_handle, g_fb_handle, 0, FB_BYTES, DMA_TO_DEVICE);
    if (res.status != ZUZU_OK) {
        LOG_ERROR(LOG_TAG, "DmaMap failed: %d", res.status);
        return res.status;
    }
    LOG_INFO(LOG_TAG, "framebuffer at bus address %x", res.bus_addr);

    DrawColorBars(g_fb);

    pl111->timing[0] = 0x2F0F5F9C;
    pl111->timing[1] = 0x210A05DF;
    pl111->timing[2] = 0x027F0000;
    pl111->upbase = res.bus_addr;

    uint32_t control = PL111_CTRL_BPP_565 | PL111_CTRL_TFT;
    pl111->control = control;
    pl111->control = control | PL111_CTRL_EN;
    Sleep(20);
    pl111->control = control | PL111_CTRL_EN | PL111_CTRL_PWR;

    LOG_INFO(LOG_TAG, "scanout running, UPCURR %x", pl111->upcurr);
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

    Sleep(3000);
    
    return 0;
}

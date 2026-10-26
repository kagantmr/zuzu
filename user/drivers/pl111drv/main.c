#include "pl111drv.h"
#include <dev/devm.h>
#include <dev/protocols/fb.h>
#include <dev/protocols/fbdrv.h>
#include <string.h>
#include <types.h>
#include <zuzu/msg.h>
#include <zuzu/service.h>
#include <zuzu/zuzu.h>

#define LOG_TAG "pl111drv"
#include <util/log.h>

#define BIT_IRQ 0
#define BIT_KICK 1
#define MASK(bit) (1U << (bit))
#define POLL_MS 10

static const Pl111Mode kMode = {
    .width = 1024,
    .height = 768,
    .hfp = 16,
    .hsw = 96,
    .hbp = 48,
    .vfp = 10,
    .vsw = 2,
    .vbp = 33,
};

static Handle g_device_handle = -1;
static Pl111Mmio *pl111;
static Handle g_fb_handle = -1;
static uint32_t g_buffers;
static uint32_t g_buf_size;
static uintptr_t g_bus_addr;
static uint32_t g_dma_id;
static bool g_dma_mapped;
static bool g_scanout_on;
static Handle g_event = -1;
static FbDrvConn g_conn = {.ctl_mem = -1, .doorbell = -1, .live = -1};
static uint32_t g_applied_seq;
static bool g_flip_pending;
static bool g_vsync_on;

static Err DevsvcHandshake(void)
{
    Handle devsvc_h;
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

    void *mmio = MemMap(g_device_handle, 0, PROT_RW);
    if (PtrIsErr(mmio)) {
        LOG_ERROR(LOG_TAG, "Could not map device MMIO");
        return (Err)mmio;
    }
    pl111 = mmio;
    return ZUZU_OK;
}

static Err AllocFramebuffer(void)
{
    uint32_t image_pages = (kMode.width * kMode.height * (PL111_BPP / 8U) + 4095U) / 4096U;
    g_buf_size = image_pages * 4096U;

    for (g_buffers = FB_MAX_BUFFERS; g_buffers >= 1; g_buffers--) {
        g_fb_handle = CreateMem(image_pages * g_buffers, MEM_CONTIG | MEM_UNCACHED);
        if (g_fb_handle >= 0)
            break;
    }
    if (g_fb_handle < 0) {
        LOG_ERROR(LOG_TAG, "framebuffer alloc failed: %d", g_fb_handle);
        return g_fb_handle;
    }

    void *fb = MemMap(g_fb_handle, 0, PROT_RW);
    if (PtrIsErr(fb)) {
        LOG_ERROR(LOG_TAG, "framebuffer map failed");
        return (Err)fb;
    }
    memset(fb, 0, (size_t)g_buf_size * g_buffers);
    MemUnmap(fb);

    DmaMapResult res =
        DmaMap(g_device_handle, g_fb_handle, 0, (size_t)g_buf_size * g_buffers, DMA_TO_DEVICE);
    if (res.status != ZUZU_OK) {
        LOG_ERROR(LOG_TAG, "DmaMap failed: %d", res.status);
        return res.status;
    }
    g_dma_mapped = true;
    g_dma_id = res.id;
    g_bus_addr = res.bus_addr;
    LOG_INFO(LOG_TAG, "%u x %u, %u image(s) at bus address %x", kMode.width, kMode.height,
             g_buffers, g_bus_addr);
    return ZUZU_OK;
}

static Err Pl111Setup(void)
{
    uint32_t part = (pl111->periph_id[0] & 0xFF) | ((pl111->periph_id[1] & 0xF) << 8);
    uint32_t magic = (pl111->pcell_id[0] & 0xFF) | ((pl111->pcell_id[1] & 0xFF) << 8) |
                     ((pl111->pcell_id[2] & 0xFF) << 16) |
                     ((uint32_t)(pl111->pcell_id[3] & 0xFF) << 24);
    if (magic != PL111_PCELL_MAGIC || part != PL111_PART_ID) {
        LOG_ERROR(LOG_TAG, "not a PL111: part %x magic %x", part, magic);
        return ERR_SYSDOWN;
    }

    Err rc = AllocFramebuffer();
    if (rc != ZUZU_OK)
        return rc;

    pl111->imsc = 0;
    pl111->icr = PL111_INT_ALL;
    pl111->timing[0] = Pl111Timing0(&kMode);
    pl111->timing[1] = Pl111Timing1(&kMode);
    pl111->timing[2] = Pl111Timing2(&kMode);
    pl111->timing[3] = 0;
    pl111->upbase = g_bus_addr;

    uint32_t control = PL111_CTRL_BPP_565 | PL111_CTRL_TFT | PL111_PIXEL_ORDER;
    pl111->control = control;
    pl111->control = control | PL111_CTRL_EN;
    g_scanout_on = true;
    Sleep(PL111_POWER_DELAY_MS);
    pl111->control = control | PL111_CTRL_EN | PL111_CTRL_PWR;

    LOG_INFO(LOG_TAG, "scanout running, UPCURR %x", pl111->upcurr);
    return ZUZU_OK;
}

static void Pl111Teardown(void)
{
    if (g_scanout_on) {
        pl111->imsc = 0;
        pl111->icr = PL111_INT_ALL;
        pl111->control &= ~PL111_CTRL_PWR;
        Sleep(PL111_POWER_DELAY_MS);
        pl111->control &= ~PL111_CTRL_EN;
        g_scanout_on = false;
    }
    if (g_dma_mapped) {
        DmaUnmap(g_device_handle, g_dma_id);
        g_dma_mapped = false;
    }
    if (g_fb_handle >= 0) {
        HandleClose(g_fb_handle);
        g_fb_handle = -1;
    }
}

static void UpdateIrqMask(void)
{
    uint32_t mask = 0;
    if (g_flip_pending)
        mask |= PL111_INT_LNBU;
    if (g_vsync_on)
        mask |= PL111_INT_VCOMP;
    pl111->imsc = mask;
}

static bool g_gfx_gone;

static void RingGfx(void)
{
    if (Signal(g_conn.doorbell, MASK(g_conn.bit), false) == ERR_DEAD)
        g_gfx_gone = true;
}

static void HandleIrq(void)
{
    uint32_t status = pl111->mis;
    pl111->icr = status;

    bool ring = false;
    if (status & PL111_INT_LNBU) {
        g_flip_pending = false;
        g_conn.ctl->flip_ack = g_applied_seq;
        ring = true;
    }
    if (status & PL111_INT_VCOMP) {
        g_conn.ctl->vsync_seq++;
        ring = true;
    }
    if (status & PL111_INT_FUF)
        LOG_ERROR(LOG_TAG, "FIFO underflow");
    if (status & PL111_INT_MBERROR)
        LOG_ERROR(LOG_TAG, "bus error");

    UpdateIrqMask();
    if (ring) {
        FBDRV_FENCE();
        RingGfx();
    }
    IrqRearm(g_device_handle);
}

static void ApplyRequests(void)
{
    FbDrvCtl *ctl = g_conn.ctl;
    FBDRV_FENCE();

    bool want_vsync = ctl->vsync_on != 0;
    if (want_vsync != g_vsync_on) {
        g_vsync_on = want_vsync;
        pl111->icr = PL111_INT_VCOMP;
        UpdateIrqMask();
    }

    uint32_t seq = ctl->flip_seq;
    if (seq == g_applied_seq || g_flip_pending)
        return;
    uint32_t index = ctl->flip_index;
    if (index >= g_buffers) {
        ctl->flip_ack = seq;
        g_applied_seq = seq;
        FBDRV_FENCE();
        RingGfx();
        return;
    }
    g_applied_seq = seq;
    g_flip_pending = true;
    pl111->icr = PL111_INT_LNBU;
    pl111->upbase = g_bus_addr + ((uintptr_t)index * g_buf_size);
    UpdateIrqMask();
}

static Err GfxConnect(void)
{
    g_event = CreateEvent();
    if (g_event < 0)
        return g_event;

    Err rc = BindIrq(g_event, g_device_handle, BIT_IRQ);
    if (rc != ZUZU_OK) {
        LOG_ERROR(LOG_TAG, "irq bind failed: %d", rc);
        return rc;
    }

    Handle gfx_port;
    for (;;) {
        gfx_port = LookupService("/svc/gfx");
        if (gfx_port >= 0)
            break;
        Sleep(10);
    }

    FbDrvCtl info = {
        .width = kMode.width,
        .height = kMode.height,
        .stride = kMode.width * (PL111_BPP / 8U),
        .format = FB_FMT_RGB565,
        .buffers = g_buffers,
        .buf_size = g_buf_size,
    };
    rc = FbDrvConnect(gfx_port, &info, g_fb_handle, g_event, BIT_KICK, &g_conn);
    if (rc != ZUZU_OK)
        LOG_ERROR(LOG_TAG, "gfxsvc handshake failed: %d", rc);
    return rc;
}

int main(void)
{
    Err rc = DevsvcHandshake();
    if (rc < 0)
        return rc;

    rc = Pl111Setup();
    if (rc < 0) {
        Pl111Teardown();
        return rc;
    }

    rc = GfxConnect();
    if (rc < 0) {
        Pl111Teardown();
        return rc;
    }

    LOG_INFO(LOG_TAG, "connected to gfxsvc");

    for (;;) {
        EventWaitResult ev = FormatToEventWait(WaitOn(g_event, POLL_MS));
        if (ev.status == ZUZU_OK && (ev.bits & MASK(BIT_IRQ)))
            HandleIrq();
        ApplyRequests();

        if (g_gfx_gone) {
            LOG_INFO(LOG_TAG, "gfxsvc went away, shutting down");
            break;
        }
    }

    FbDrvDisconnect(&g_conn);
    Pl111Teardown();
    return 0;
}

#include <dev/protocols/fb.h>
#include <stdio.h>
#include <string.h>
#include <zuzu/msg.h>
#include <zuzu/service.h>
#include <zuzu/zuzu.h>

#define LOG(fmt, ...) printf("fbtest: " fmt "\n" __VA_OPT__(,) __VA_ARGS__)

static Handle g_gfx = -1;
static uint32_t g_token;

static Err Call2(uint32_t cmd, uint32_t arg, Handle grant, void *reply, uint32_t reply_len,
                 Handle *reply_grant)
{
    FbRequest req = {.cmd = cmd, .token = g_token, .arg = arg};
    MsgWrite(&req, sizeof(req));
    uint32_t got = 0;
    Err rc = CallPort(g_gfx, sizeof(req), grant, &got, reply_grant);
    if (rc != ZUZU_OK)
        return rc;
    if (got < sizeof(Err))
        return ERR_MALFORMED;
    Err status;
    memcpy(&status, GetMessageBox(), sizeof(status));
    if (reply && got >= reply_len)
        memcpy(reply, GetMessageBox(), reply_len);
    return status;
}

static Err CallGrant(uint32_t cmd, uint32_t arg, Handle obj, HandlePerms perms)
{
    Handle dup;
    Err rc = HandleDup(obj, perms | PERM_TXFR, MARKER_NONE, &dup);
    if (rc != ZUZU_OK)
        return rc;
    rc = Call2(cmd, arg, dup, NULL, 0, NULL);
    HandleClose(dup);
    return rc;
}

static void Fill(volatile uint16_t *img, const FbInfo *info, uint16_t color)
{
    uint32_t px = info->stride / 2U;
    for (uint32_t y = 0; y < info->height; y++)
        for (uint32_t x = 0; x < info->width; x++)
            img[(y * px) + x] = color;
}

static uint32_t WaitBits(Handle ev, uint32_t want, uint32_t timeout_ms)
{
    for (uint32_t waited = 0; waited < timeout_ms; waited += 20) {
        EventWaitResult r = FormatToEventWait(WaitOn(ev, 20));
        if (r.status == ZUZU_OK && (r.bits & want))
            return r.bits & want;
    }
    return 0;
}

int main(void)
{
    for (int i = 0; i < 500 && g_gfx < 0; i++) {
        g_gfx = LookupService("/svc/gfx");
        if (g_gfx < 0)
            Sleep(10);
    }
    if (g_gfx < 0) {
        LOG("no /svc/gfx");
        return 1;
    }

    FbInfo info;
    Err rc = Call2(FB_CMD_GET_INFO, 0, -1, &info, sizeof(info), NULL);
    if (rc != ZUZU_OK) {
        LOG("GET_INFO rc=%d", rc);
        return 1;
    }
    LOG("%ux%u stride %u fmt %u, %u image(s) of %u bytes", info.width, info.height, info.stride,
        info.format, info.buffers, info.buf_size);

    Handle live = CreatePort();
    Handle ev = CreateEvent();
    Handle mem = -1;
    Handle dup;
    rc = HandleDup(live, PERM_WAIT | PERM_TXFR, MARKER_NONE, &dup);
    if (rc == ZUZU_OK) {
        rc = Call2(FB_CMD_GET_BUF, 0, dup, &info, sizeof(info), &mem);
        HandleClose(dup);
    }
    if (rc != ZUZU_OK || mem < 0) {
        LOG("GET_BUF rc=%d", rc);
        return 1;
    }
    g_token = info.token;

    Handle again;
    HandleDup(live, PERM_WAIT | PERM_TXFR, MARKER_NONE, &again);
    FbInfo second;
    rc = Call2(FB_CMD_GET_BUF, 0, again, &second, sizeof(second), NULL);
    HandleClose(again);
    LOG("second GET_BUF from owner rc=%d (expect %d)", rc, ERR_BUSY);

    uint8_t *base = MemMap(mem, 0, PROT_RW);
    if (PtrIsErr(base)) {
        LOG("map failed");
        return 1;
    }

    rc = CallGrant(FB_CMD_SET_EVENT, 0, ev, PERM_SEND);
    LOG("SET_EVENT rc=%d", rc);

    Fill((uint16_t *)base, &info, 0xF800);
    LOG("image 0 = 0xF800 (should be RED)");
    Sleep(1500);

    rc = Call2(FB_CMD_SET_VSYNC, 1, -1, NULL, 0, NULL);
    LOG("SET_VSYNC rc=%d", rc);
    uint32_t vsyncs = 0;
    for (int i = 0; i < 100; i++) {
        if (WaitBits(ev, FB_EVT_VSYNC, 20))
            vsyncs++;
    }
    Call2(FB_CMD_SET_VSYNC, 0, -1, NULL, 0, NULL);
    LOG("vsync events in 2s: %u", vsyncs);

    if (info.buffers >= 2) {
        Fill((uint16_t *)(base + info.buf_size), &info, 0x07E0);
        rc = Call2(FB_CMD_FLIP, 1, -1, NULL, 0, NULL);
        uint32_t got = WaitBits(ev, FB_EVT_FLIPPED, 500);
        LOG("FLIP 1 rc=%d, FLIPPED %s (image 1 = 0x07E0, should be GREEN)", rc,
            got ? "arrived" : "TIMED OUT");
        Sleep(1500);

        Fill((uint16_t *)base, &info, 0x001F);
        rc = Call2(FB_CMD_FLIP, 0, -1, NULL, 0, NULL);
        got = WaitBits(ev, FB_EVT_FLIPPED, 500);
        LOG("FLIP 0 rc=%d, FLIPPED %s (image 0 = 0x001F, should be BLUE)", rc,
            got ? "arrived" : "TIMED OUT");
        Sleep(1500);
    }

    rc = Call2(FB_CMD_RELEASE, 0, -1, NULL, 0, NULL);
    LOG("RELEASE rc=%d, done", rc);
    return 0;
}

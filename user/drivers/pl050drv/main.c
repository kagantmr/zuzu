#include "kmi.h"
#include "ps2dec.h"
#include <dev/devm.h>
#include <dev/keymap.h>
#include <dev/tty.h>
#include <util/log.h>
#include <zuzu/shm_ring.h>
#include <zuzu/service.h>
#include <zuzu/uptime.h>
#include <zuzu/zuzu.h>

#define LOG_TAG "pl050drv"
static const char *const DEV_COMPAT[] = {"arm,pl050"};

typedef enum { KMI_ROLE_NONE = 0, KMI_ROLE_KBD = 1, KMI_ROLE_MOUSE = 2 } KmiRole;

typedef struct {
    volatile Pl050Mmio *kmi;
    Handle kmi_dev_h;
    Ps2Decoder dec;
    KeymapState keymap;
    KmiRole role;
    uint8_t irq_pos;
} KmiPort;

/**
 * shared, we bind 2 irqs into 1 event
 */
static Handle g_kmi_irq_ev = -1;
static KmiPort ports[2];
static Handle g_devsvc_port = -1;

#define BIT_TTY 2
#define TTY_ALIAS "fbcons"
static TtyConn g_tty;
static bool g_tty_up;

#define KMI_TX_TIMEOUT_MS 20

static Err KmiTx(KmiPort *p, uint8_t byte)
{
    uint64_t start = UptimeMs();
    while (!(p->kmi->stat & KMISTAT_TXEMPTY)) {
        if (UptimeMs() - start > KMI_TX_TIMEOUT_MS)
            return ERR_TIMEOUT;
    }
    p->kmi->data = byte;
    return ZUZU_OK;
}

static int KmiRx(KmiPort *p, Duration to_ms)
{
    uint64_t start = UptimeMs();
    for (;;) {
        uint32_t stat = p->kmi->stat;
        if (stat & KMISTAT_RXFULL) {
            uint8_t byte = (uint8_t)p->kmi->data;
            return byte;
        }
        if (UptimeMs() - start >= to_ms)
            return ERR_TIMEOUT;
        Sleep(1);
    }
}

static int KmiFlush(KmiPort *p)
{
    while (p->kmi->stat & KMISTAT_RXFULL) {
        uint32_t data = p->kmi->data;
        (void)data;
    }
    return 0;
}

#define KMI_ACK 0xFA
#define KMI_RESEND 0xFE
#define KMI_CMD_RETRIES 3
#define KMI_ACK_TIMEOUT_MS 50

static Err KmiSendAck(KmiPort *p, uint8_t cmd)
{
    KmiFlush(p);
    for (int attempt = 0; attempt < KMI_CMD_RETRIES; attempt++) {
        Err rc = KmiTx(p, cmd);
        if (rc != ZUZU_OK)
            return rc;

        int reply = KmiRx(p, KMI_ACK_TIMEOUT_MS);
        if (reply == KMI_ACK)
            return ZUZU_OK;
        if (reply == ERR_TIMEOUT)
            return ERR_TIMEOUT;
        if (reply != KMI_RESEND && reply != ERR_IO)
            return ERR_MALFORMED;
    }
    return ERR_IO;
}

static Err PortOpen(KmiPort *p, uint32_t nth)
{
    uint32_t matched1;
    p->kmi_dev_h = RequestDeviceNth(g_devsvc_port, DEV_COMPAT, 1, nth, &matched1);
    if (p->kmi_dev_h < 0) {
        LOG_ERROR(LOG_TAG, "Couldn't get device");
        HandleClose(p->kmi_dev_h);
        return p->kmi_dev_h;
    }

    p->kmi = MemMap(p->kmi_dev_h, 0, PROT_RW);
    if (PtrIsErr((const void *)p->kmi)) {
        LOG_ERROR(LOG_TAG, "Couldn't map device");
        HandleClose(p->kmi_dev_h);
        return (Err)p->kmi;
    }

    Err rc = BindIrq(g_kmi_irq_ev, p->kmi_dev_h, nth);
    if (rc < 0) {
        MemUnmap((void *)p->kmi);
        HandleClose(p->kmi_dev_h);
        return rc;
    }

    p->irq_pos = nth;
    return ZUZU_OK;
}

static const char *RoleName(KmiRole role)
{
    switch (role) {
    case KMI_ROLE_KBD:
        return "keyboard";
    case KMI_ROLE_MOUSE:
        return "mouse";
    case KMI_ROLE_NONE:
        break;
    }
    return "none";
}

static Err PortProbe(KmiPort *p)
{
    unsigned n = (unsigned)p->irq_pos;
    p->role = KMI_ROLE_NONE;

    p->kmi->cr = 0;
    p->kmi->clk = 100;
    p->kmi->cr = KMICR_EN;
    KmiFlush(p);

    Err rc = KmiSendAck(p, 0xFF);
    if (rc == ERR_TIMEOUT) {
        LOG_INFO(LOG_TAG, "port %u: no device", n);
        return ZUZU_OK;
    }
    if (rc != ZUZU_OK) {
        LOG_ERROR(LOG_TAG, "port %u: reset failed: %s", n, StrToError(rc));
        return rc;
    }

    int bat = KmiRx(p, 1000);
    if (bat != 0xAA) {
        LOG_ERROR(LOG_TAG, "port %u: self-test failed (reply %d)", n, bat);
        return ERR_IO;
    }
    int extra = KmiRx(p, 100);
    LOG_DEBUG(LOG_TAG, "port %u: after self-test %d", n, extra);

    rc = KmiSendAck(p, 0xF5);
    if (rc != ZUZU_OK) {
        LOG_ERROR(LOG_TAG, "port %u: disable scanning failed: %s", n, StrToError(rc));
        return rc;
    }
    rc = KmiSendAck(p, 0xF2);
    if (rc != ZUZU_OK) {
        LOG_ERROR(LOG_TAG, "port %u: identify failed: %s", n, StrToError(rc));
        return rc;
    }

    int id0 = KmiRx(p, 50);
    if (id0 == 0xAB) {
        int id1 = KmiRx(p, 50);
        if (id1 != 0x83 && id1 != 0xC1)
            LOG_WARN(LOG_TAG, "port %u: unexpected keyboard id byte %d", n, id1);
        else
            LOG_INFO(LOG_TAG, "port %u: keyboard, id ab %02x", n, id1);
        p->role = KMI_ROLE_KBD;
    } else if (id0 == 0x00 || id0 == 0x03 || id0 == 0x04) {
        LOG_INFO(LOG_TAG, "port %u: mouse, id %02x", n, id0);
        p->role = KMI_ROLE_MOUSE;
    } else {
        LOG_INFO(LOG_TAG, "port %u: keyboard, no id (reply %d)", n, id0);
        p->role = KMI_ROLE_KBD;
    }

    if (p->role == KMI_ROLE_KBD) {
        rc = KmiSendAck(p, 0xF4);
        if (rc != ZUZU_OK) {
            LOG_ERROR(LOG_TAG, "port %u: enable scanning failed: %s", n, StrToError(rc));
            p->role = KMI_ROLE_NONE;
            return rc;
        }
        Ps2DecoderInit(&p->dec);
        KeymapInit(&p->keymap);
    }
    return ZUZU_OK;
}

static void PortDrain(KmiPort *p)
{
    bool pushed = false;
    while (p->kmi->stat & KMISTAT_RXFULL) {
        uint8_t byte = (uint8_t)p->kmi->data;
        if (p->role != KMI_ROLE_KBD)
            continue;

        Ps2KeyEvent ev;
        if (!Ps2DecoderFeed(&p->dec, byte, &ev))
            continue;
        // LOG_DEBUG(LOG_TAG, "port %u usage %02x %s", (unsigned)p->irq_pos, ev.usage, ev.pressed ? "down" : "up");

        uint8_t seq[KEYMAP_MAX_SEQ];
        size_t n = KeymapFeed(&p->keymap, ev.usage, ev.pressed, seq, sizeof(seq));
        if (n == 0 || !g_tty_up)
            continue;
        if (ShmRingPush(&g_tty.shm->up_hdr, g_tty.shm->up_data, seq, (uint32_t)n) != n)
            LOG_WARN(LOG_TAG, "tty input ring full, key dropped");
        pushed = true;
    }
    if (pushed)
        Signal(g_tty.doorbell, 1U << g_tty.bit, false);
}

static void TtyConnect(void)
{
    Handle tty_port;
    for (;;) {
        tty_port = LookupService("/svc/tty");
        if (tty_port >= 0)
            break;
        Sleep(10);
    }

    Err rc = TtyClientConnectDir(tty_port, TTY_PROVIDE, TTY_ALIAS, TTY_DIR_IN, g_kmi_irq_ev,
                                 BIT_TTY, &g_tty);
    if (rc != ZUZU_OK) {
        LOG_ERROR(LOG_TAG, "tty provide failed: %s", StrToError(rc));
        return;
    }
    g_tty_up = true;
    LOG_INFO(LOG_TAG, "feeding tty endpoint %s", TTY_ALIAS);
}

int main()
{
    for (;;) {
        g_devsvc_port = LookupService("/svc/devsvc");
        if (g_devsvc_port >= 0)
            break;
        Sleep(10);
    }

    g_kmi_irq_ev = CreateEvent();
    if (g_kmi_irq_ev < 0)
        return g_kmi_irq_ev;

    for (uint32_t i = 0; i < 2; i++) {
        Err rc = PortOpen(&ports[i], i);
        if (rc != ZUZU_OK) {
            LOG_ERROR(LOG_TAG, "Couldn't open KMI port %u: %s", (unsigned)i, StrToError(rc));
            return rc;
        }
    }

    for (uint32_t i = 0; i < 2; i++) {
        Err rc = PortProbe(&ports[i]);
        if (rc != ZUZU_OK)
            LOG_ERROR(LOG_TAG, "KMI port %u unusable: %s", (unsigned)i, StrToError(rc));
    }

    LOG_INFO(LOG_TAG, "port 0: %s, port 1: %s", RoleName(ports[0].role), RoleName(ports[1].role));
    if (ports[0].role == KMI_ROLE_NONE && ports[1].role == KMI_ROLE_NONE) {
        LOG_ERROR(LOG_TAG, "no keyboard or mouse found");
        return ERR_NOENT;
    }

    if (ports[0].role == KMI_ROLE_KBD || ports[1].role == KMI_ROLE_KBD)
        TtyConnect();

    if (ports[0].role) ports[0].kmi->cr = KMICR_EN | KMICR_RXINTREN;
    if (ports[1].role) ports[1].kmi->cr = KMICR_EN | KMICR_RXINTREN;
        
    for (;;) {
        EventWaitResult r = WaitOnEvent(g_kmi_irq_ev, TIMEOUT_INFINITE, 0, false);
        if (r.status != ZUZU_OK)
            continue;

        if (r.bits & (1U << 0)) {
            PortDrain(&ports[0]);
            IrqRearm(ports[0].kmi_dev_h);
        }

        if (r.bits & (1U << 1))  {
            PortDrain(&ports[1]);
            IrqRearm(ports[1].kmi_dev_h);
        }
        
    }
    return 0;
}

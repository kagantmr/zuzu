#include "ctl.h"
#include "common/globals.h"
#include "common/netmq.h"
#include <net/protocols/netd.h>
#include <net/socket.h>
#include <string.h>
#include <util/log.h>
#include <zuzu/msg.h>
#include <zuzu/zuzu.h>
#include <ztask/ztask.h>

static NetdMessageQueue g_mq;
static Ticket g_cur_ticket;

/* A driver between NETD_DRVHANDSHAKE_INIT and STAGE3, matched by its marker. */
typedef struct {
    bool active;
    Marker sender;
    Handle doorbell;
    MacAddr mac;
    uint64_t key;
    uint8_t type;
    NicRing *tx_ring;
} PendingDriver;

static PendingDriver g_pending[NETIF_MAX];

/* Which driver key owns each netifs[] slot; only the ctl thread allocates slots. */
static struct {
    bool used;
    uint64_t key;
    uint8_t type;
    uint8_t unit; /* the N in eth<N> */
} g_slots[NETIF_MAX];

static const char *const g_type_prefix[NETD_IFTYPE_COUNT] = {"eth", "wlan", "pwire"};

/* Lowest unit number no slot of this type holds. */
static uint8_t FreeUnit(uint8_t type)
{
    for (uint8_t unit = 0;; unit++) {
        bool taken = false;
        for (int i = 0; i < NETIF_MAX; i++)
            if (g_slots[i].used && g_slots[i].type == type && g_slots[i].unit == unit)
                taken = true;
        if (!taken)
            return unit;
    }
}

static void NameFor(char out[NETIF_NAME_MAX], uint8_t type, uint8_t unit)
{
    const char *prefix = g_type_prefix[type];
    size_t n = strlen(prefix);
    memcpy(out, prefix, n);
    out[n] = (char)('0' + unit);
    out[n + 1] = '\0';
}

static void CtlRefuse(const PortWaitResult *r, Err err)
{
    if (r->granted >= 0)
        HandleClose(r->granted);
    MsgWrite(&err, sizeof(err));
    Reply(sizeof(err), -1);
}

static void CtlReplyErr(Err err)
{
    MsgWrite(&err, sizeof(err));
    Reply(sizeof(err), -1);
}

static PendingDriver *PendingFind(Marker sender)
{
    for (int i = 0; i < NETIF_MAX; i++)
        if (g_pending[i].active && g_pending[i].sender == sender)
            return &g_pending[i];
    return NULL;
}

static void PendingDrop(PendingDriver *p)
{
    if (p->doorbell >= 0)
        HandleClose(p->doorbell);
    if (p->tx_ring)
        MemUnmap(p->tx_ring);
    memset(p, 0, sizeof(*p));
    p->doorbell = -1;
}

static int SlotFor(uint64_t key, bool *reattach)
{
    for (int i = 0; i < NETIF_MAX; i++) {
        if (g_slots[i].used && g_slots[i].key == key) {
            *reattach = true;
            return i;
        }
    }
    for (int i = 0; i < NETIF_MAX; i++) {
        if (!g_slots[i].used) {
            *reattach = false;
            return i;
        }
    }
    return -1;
}

static void DriverInit(const PortWaitResult *r)
{
    if (r->granted < 0 || r->xlen < 9) {
        CtlRefuse(r, ERR_BADARG);
        return;
    }
    PendingDriver *p = PendingFind(r->sender);
    if (p) {
        PendingDrop(p); /* the driver restarted mid-handshake */
    } else {
        for (int i = 0; i < NETIF_MAX && !p; i++)
            if (!g_pending[i].active)
                p = &g_pending[i];
        if (!p) {
            CtlRefuse(r, ERR_BUSY);
            return;
        }
    }

    const uint8_t *msg = GetMessageBox();
    p->active = true;
    p->sender = r->sender;
    p->doorbell = r->granted;
    memcpy(&p->mac[0], msg + 1, 4);
    memcpy(&p->mac[4], msg + 5, 2);
    p->key = 0;
    if (r->xlen >= 17)
        memcpy(&p->key, msg + 9, sizeof(p->key));
    if (p->key == 0)
        for (int i = 0; i < MAC_ADDR_LEN; i++)
            p->key = (p->key << 8) | p->mac[i];
    p->type = r->xlen >= 18 ? msg[17] : NETD_IFTYPE_ETH;
    if (p->type >= NETD_IFTYPE_COUNT) {
        PendingDrop(p);
        CtlReplyErr(ERR_BADARG);
        return;
    }
    p->tx_ring = NULL;
    CtlReplyErr(ZUZU_OK);
}

static void DriverStage2(const PortWaitResult *r)
{
    PendingDriver *p = PendingFind(r->sender);
    if (!p || p->tx_ring || r->granted < 0) {
        CtlRefuse(r, ERR_BADARG);
        return;
    }
    NicRing *tx = (NicRing *)MemMap(r->granted, 0, PROT_RW);
    if (PtrIsErr(tx)) {
        HandleClose(r->granted);
        PendingDrop(p);
        CtlReplyErr(ERR_BADHANDLE);
        return;
    }
    p->tx_ring = tx;
    CtlReplyErr(ZUZU_OK);
}

static void DriverStage3(const PortWaitResult *r)
{
    PendingDriver *p = PendingFind(r->sender);
    if (!p || !p->tx_ring || r->granted < 0) {
        CtlRefuse(r, ERR_BADARG);
        return;
    }
    NicRing *rx = (NicRing *)MemMap(r->granted, 0, PROT_RW);
    if (PtrIsErr(rx)) {
        HandleClose(r->granted);
        PendingDrop(p);
        CtlReplyErr(ERR_BADHANDLE);
        return;
    }

    bool reattach;
    int slot = SlotFor(p->key, &reattach);
    Handle bell = -1;
    Err rc = slot < 0 ? ERR_NOMEM : HandleDup(g_event, PERM_SEND | PERM_TXFR, MARKER_NONE, &bell);
    if (rc != ZUZU_OK) {
        MemUnmap(rx);
        PendingDrop(p);
        CtlReplyErr(rc);
        return;
    }

    CtlAttach a = {.op = CTL_OP_ATTACH,
                   .slot = (uint8_t)slot,
                   .reattach = reattach,
                   .tx_ring = p->tx_ring,
                   .rx_ring = rx,
                   .doorbell = p->doorbell};
    memcpy(a.mac, p->mac, sizeof(a.mac));
    uint8_t unit = reattach ? g_slots[slot].unit : FreeUnit(p->type);
    NameFor(a.name, p->type, unit);

    NetdMsg m = {.op = CTL_OP_ATTACH, .sender = r->sender, .granted = -1, .len = sizeof(a)};
    memcpy(m.body, &a, sizeof(a));
    if (NetMqPush(&g_mq, &m) != ZUZU_OK) {
        HandleClose(bell);
        MemUnmap(rx);
        PendingDrop(p);
        CtlReplyErr(ERR_BUSY);
        return;
    }
    g_slots[slot].used = true;
    g_slots[slot].key = p->key;
    g_slots[slot].type = p->type;
    g_slots[slot].unit = unit;
    memset(p, 0, sizeof(*p)); /* tcpip owns the rings and doorbell now */
    p->doorbell = -1;
    Signal(g_event, CTL_MBOX_MASK, false);

    MsgWriter w;
    MsgWriterInit(&w);
    MsgPutU32(&w, ZUZU_OK);
    MsgPutStr(&w, a.name);
    Reply(w.off, bell);
    HandleClose(bell);
    LOG_INFO(LOG_TAG, "%s: driver %s", a.name, reattach ? "reattached" : "attached");
}

/* Runs a handshake stage if the call is one; false for every other call. */
static bool CtlDriverCall(const PortWaitResult *r)
{
    if (r->xlen < 1)
        return false;
    uint32_t cmd = 0;
    if (r->xlen >= sizeof(cmd)) {
        memcpy(&cmd, GetMessageBox(), sizeof(cmd));
        if (SOCK_IS_CMD(cmd))
            return false;
    }
    switch (*(const uint8_t *)GetMessageBox()) {
    case NETD_DRVHANDSHAKE_INIT:
        DriverInit(r);
        return true;
    case NETD_DRVHANDSHAKE_STAGE2:
        DriverStage2(r);
        return true;
    case NETD_DRVHANDSHAKE_STAGE3:
        DriverStage3(r);
        return true;
    case CTL_OP_ATTACH:
        CtlRefuse(r, ERR_BADARG);
        return true;
    default:
        return false;
    }
}

static void CtlMain(void *arg)
{
    (void)arg;
    for (;;) {
        PortWaitResult r = WaitOnPort(g_svc_port, TIMEOUT_INFINITE);
        if (r.status != ZUZU_OK)
            continue;
        if (CtlDriverCall(&r))
            continue;
        if (r.xlen > NETMQ_BODY_MAX) {
            CtlRefuse(&r, ERR_BADARG);
            continue;
        }

        NetdMsg m = {.sender = r.sender, .granted = r.granted, .len = (uint16_t)r.xlen};
        memcpy(m.body, GetMessageBox(), r.xlen);
        m.op = r.xlen ? (uint8_t)m.body[0] : 0;

        Err rc = Hold(&m.ticket);
        if (rc != ZUZU_OK) {
            if (rc == ERR_DEAD) {
                if (r.granted >= 0)
                    HandleClose(r.granted);
            } else {
                CtlRefuse(&r, ERR_BUSY);
            }
            continue;
        }

        if (NetMqPush(&g_mq, &m) != ZUZU_OK) {
            if (r.granted >= 0)
                HandleClose(r.granted);
            Err busy = ERR_BUSY;
            MsgWrite(&busy, sizeof(busy));
            ReplyTo(m.ticket, sizeof(busy), -1);
            continue;
        }
        Signal(g_event, CTL_MBOX_MASK, false);
    }
}

Err CtlStart(void)
{
    NetMqInit(&g_mq);
    for (int i = 0; i < NETIF_MAX; i++)
        g_pending[i].doorbell = -1;
    Err rc = ZTaskSpawnDetached(CtlMain, NULL, 0);
    if (rc != ZUZU_OK)
        LOG_ERROR(LOG_TAG, "ctl thread failed to start");
    return rc;
}

bool CtlNextCall(PortWaitResult *call)
{
    NetdMsg m;
    if (NetMqPop(&g_mq, &m) != ZUZU_OK)
        return false;
    memcpy(GetMessageBox(), m.body, m.len);
    g_cur_ticket = m.ticket;
    *call = (PortWaitResult){
        .status = ZUZU_OK, .sender = m.sender, .xlen = m.len, .granted = m.granted};
    return true;
}

Err CtlReply(uint32_t xlen, Handle grant) { return ReplyTo(g_cur_ticket, xlen, grant); }

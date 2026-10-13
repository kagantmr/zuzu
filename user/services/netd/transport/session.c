#include "session.h"
#include "../common/globals.h"
#include "socket.h"
#include <string.h>
#include <util/log.h>
#include <util/msg.h>

#define SESSION_BIT(slot) ((uint32_t)(slot) + 1U)
#define SESSION_INDEX(gen, slot) ((((uint32_t)(gen)) << 8) | ((uint32_t)(slot) & 0xFFu))
#define INDEX_SLOT(index) ((uint32_t)(index) & 0xFFu)
#define INDEX_GEN(index) ((uint32_t)(index) >> 8)

static Session g_sessions[SESSION_MAX];
static uint32_t g_gen[SESSION_MAX];
static Handle g_doorbell = -1;
static Handle g_death = -1;

static void ReplyStatus(Err status)
{
    MsgWrite(&status, sizeof(status));
    Reply(sizeof(status), -1);
}

static void DropGrant(const PortWaitResult *call)
{
    if (call->granted >= 0)
        HandleClose(call->granted);
}

static int AllocSession(void)
{
    for (int i = 0; i < SESSION_MAX; i++) {
        if (!g_sessions[i].in_use)
            return i;
    }
    return -1;
}

static Session *SessionFor(uint32_t index, Marker caller)
{
    uint32_t slot = INDEX_SLOT(index);
    if (slot >= SESSION_MAX || !g_sessions[slot].in_use || g_gen[slot] != INDEX_GEN(index))
        return NULL;
    Session *s = &g_sessions[slot];
    if (s->owner != MARKER_NONE && s->owner != caller)
        return NULL;
    return s;
}

static void ReleaseSession(int slot)
{
    SocketCloseAll(slot);
    Session *s = &g_sessions[slot];
    if (s->app_event >= 0)
        HandleClose(s->app_event);
    if (s->live >= 0)
        HandleClose(s->live);
    *s = (Session){.app_event = -1, .live = -1};
    ListInit(&s->sockets);
}

Err SessionInit(Handle doorbell_event)
{
    g_doorbell = doorbell_event;
    g_death = CreateEvent();
    if (g_death < 0)
        return (Err)g_death;
    for (int i = 0; i < SESSION_MAX; i++) {
        g_sessions[i] = (Session){.app_event = -1, .live = -1};
        ListInit(&g_sessions[i].sockets);
    }
    SocketInit();
    return ZUZU_OK;
}

static void HandleAttach(const PortWaitResult *call)
{
    if (call->granted < 0) {
        ReplyStatus(ERR_BADARG);
        return;
    }
    int slot = AllocSession();
    if (slot < 0) {
        DropGrant(call);
        ReplyStatus(ERR_BUSY);
        return;
    }
    SvcResult bell = HandleDuplicate(g_doorbell, PERM_SEND | PERM_TXFR, MARKER_NONE);
    if (bell.r0 != ZUZU_OK) {
        DropGrant(call);
        ReplyStatus((Err)bell.r0);
        return;
    }
    Err rc = Bind(EVENT_PORT, g_death, call->granted, (uint32_t)slot);
    if (rc != ZUZU_OK) {
        HandleClose((Handle)bell.r1);
        DropGrant(call);
        ReplyStatus(rc);
        return;
    }

    g_sessions[slot] =
        (Session){.in_use = true, .owner = call->sender, .live = call->granted, .app_event = -1};
    ListInit(&g_sessions[slot].sockets);
    g_gen[slot] = (g_gen[slot] + 1) & 0xFFFFFFU;
    if (g_gen[slot] == 0)
        g_gen[slot] = 1;

    SockAttachReply rep = {
        .status = ZUZU_OK, .session = SESSION_INDEX(g_gen[slot], slot), .bit = SESSION_BIT(slot)};
    MsgWrite(&rep, sizeof(rep));
    Err sent = Reply(sizeof(rep), (Handle)bell.r1);
    HandleClose((Handle)bell.r1);
    if (sent != ZUZU_OK) {
        LOG_INFO(LOG_TAG, "session %d: reply grant failed rc=%d", slot, sent);
        ReleaseSession(slot);
        return;
    }
    LOG_INFO(LOG_TAG, "session %d attached", slot);
}

static void HandleNotify(const PortWaitResult *call)
{
    SockNotifyRequest req;
    if (call->xlen < sizeof(req) || call->granted < 0) {
        DropGrant(call);
        ReplyStatus(ERR_BADARG);
        return;
    }
    memcpy(&req, GetMessageBox(), sizeof(req));
    Session *s = SessionFor(req.session, call->sender);
    if (!s || req.bit >= 31) {
        DropGrant(call);
        ReplyStatus(s ? ERR_BADARG : ERR_NOTCONN);
        return;
    }
    if (s->app_event >= 0)
        HandleClose(s->app_event);
    s->app_event = call->granted;
    s->app_bit = req.bit;
    s->dirty = true;
    ReplyStatus(ZUZU_OK);
}

static bool DecodeSockRequest(const PortWaitResult *call, SockRequest *req, int *slot)
{
    if (call->xlen < sizeof(*req))
        return false;
    memcpy(req, GetMessageBox(), sizeof(*req));
    if (!SessionFor(req->session, call->sender))
        return false;
    *slot = (int)INDEX_SLOT(req->session);
    return true;
}

static void HandleSockOpen(const PortWaitResult *call)
{
    SockOpenRequest req;
    if (call->xlen < sizeof(req) || call->granted < 0) {
        DropGrant(call);
        ReplyStatus(ERR_BADARG);
        return;
    }
    memcpy(&req, GetMessageBox(), sizeof(req));
    if (!SessionFor(req.session, call->sender)) {
        DropGrant(call);
        ReplyStatus(ERR_NOTCONN);
        return;
    }

    uint32_t id = 0;
    Err rc = SocketOpen((int)INDEX_SLOT(req.session), req.kind, call->granted, &id);
    if (rc != ZUZU_OK) {
        DropGrant(call);
        ReplyStatus(rc);
        return;
    }
    SockOpenReply rep = {.status = ZUZU_OK, .sock = id};
    MsgWrite(&rep, sizeof(rep));
    Reply(sizeof(rep), -1);
}

static void HandleSockBind(const PortWaitResult *call)
{
    SockRequest req;
    int slot;
    if (!DecodeSockRequest(call, &req, &slot)) {
        DropGrant(call);
        ReplyStatus(ERR_NOTCONN);
        return;
    }
    Socket *sock = SocketFor(req.sock, slot);
    if (!sock) {
        DropGrant(call);
        ReplyStatus(ERR_NOENT);
        return;
    }
    if (req.a > 0xFFFFU) {
        DropGrant(call);
        ReplyStatus(ERR_BADARG);
        return;
    }
    DropGrant(call);
    ReplyStatus(SocketBind(sock, (NetPort)req.a));
}

static void HandleSockClose(const PortWaitResult *call)
{
    SockRequest req;
    int slot;
    DropGrant(call);
    if (!DecodeSockRequest(call, &req, &slot)) {
        ReplyStatus(ERR_NOTCONN);
        return;
    }
    Socket *sock = SocketFor(req.sock, slot);
    if (!sock) {
        ReplyStatus(ERR_NOENT);
        return;
    }
    SocketClose(sock);
    ReplyStatus(ZUZU_OK);
}

bool SessionHandle(const PortWaitResult *call)
{
    uint32_t cmd = 0;
    if (call->xlen < sizeof(cmd))
        return false;
    memcpy(&cmd, GetMessageBox(), sizeof(cmd));
    if (!SOCK_IS_CMD(cmd))
        return false;

    switch (cmd) {
    case SOCK_ATTACH:
        HandleAttach(call);
        break;
    case SOCK_NOTIFY:
        HandleNotify(call);
        break;
    case SOCK_OPEN:
        HandleSockOpen(call);
        break;
    case SOCK_BIND:
        HandleSockBind(call);
        break;
    case SOCK_CLOSE:
        HandleSockClose(call);
        break;
    default:
        DropGrant(call);
        ReplyStatus(ERR_NOSYS);
        break;
    }
    return true;
}

ListHead *SessionSockets(int slot)
{
    if (slot < 0 || slot >= SESSION_MAX || !g_sessions[slot].in_use)
        return NULL;
    return &g_sessions[slot].sockets;
}

void SessionSocketCount(int slot, int delta)
{
    if (slot < 0 || slot >= SESSION_MAX || !g_sessions[slot].in_use)
        return;
    g_sessions[slot].nsockets += delta;
}

bool SessionSocketsFull(int slot)
{
    if (slot < 0 || slot >= SESSION_MAX || !g_sessions[slot].in_use)
        return true;
    return g_sessions[slot].nsockets >= SESSION_SOCKETS_MAX;
}

void SessionDoorbell(EventWord bits)
{
    for (int i = 0; i < SESSION_MAX; i++) {
        if ((bits & SESSION_BIT(i)) && g_sessions[i].in_use)
            g_sessions[i].dirty = true;
    }
}

void SessionKick(int slot)
{
    if (slot < 0 || slot >= SESSION_MAX)
        return;
    Session *s = &g_sessions[slot];
    if (!s->in_use || s->app_event < 0)
        return;
    Signal(s->app_event, 1U << s->app_bit, false);
}

void SessionReapDead(void)
{
    EventWaitResult d = FormatToEventWait(WaitOn(g_death, TIMEOUT_POLL));
    if (d.status != ZUZU_OK)
        return;
    if (d.status == ZUZU_OK)
        LOG_DEBUG(LOG_TAG, "death bits=%x", d.bits);          /* temp */
    for (int slot = 0; slot < SESSION_MAX; slot++) {
        Session *s = &g_sessions[slot];
        if (!(d.bits & (1U << slot)) || !s->in_use || s->live < 0)
            continue;
        SvcResult q = HandleQuery(s->live, QUERY_STATUS);
        LOG_DEBUG(LOG_TAG, "slot %d query r0=%d r1=%d", slot, (int)q.r0, (int)q.r1);   /* temp, inside the loop */
        if (q.r0 != ZUZU_OK || (Err)q.r1 != ERR_DEAD)
            continue;
        LOG_DEBUG(LOG_TAG, "session %d: app died", slot);
        ReleaseSession(slot);
    }
}

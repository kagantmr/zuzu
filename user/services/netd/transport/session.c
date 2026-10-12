#include "session.h"
#include "../common/globals.h"
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
    Session *s = &g_sessions[slot];
    if (s->app_event >= 0)
        HandleClose(s->app_event);
    if (s->live >= 0)
        HandleClose(s->live);
    *s = (Session){.app_event = -1, .live = -1};
}

Err SessionInit(Handle doorbell_event)
{
    g_doorbell = doorbell_event;
    g_death = CreateEvent();
    if (g_death < 0)
        return (Err)g_death;
    for (int i = 0; i < SESSION_MAX; i++)
        g_sessions[i] = (Session){.app_event = -1, .live = -1};
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

    g_sessions[slot] = (Session){
        .in_use = true, .owner = call->sender, .live = call->granted, .app_event = -1};
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
    default:
        DropGrant(call);
        ReplyStatus(ERR_NOSYS);
        break;
    }
    return true;
}

void SessionDoorbell(EventWord bits)
{
    for (int i = 0; i < SESSION_MAX; i++) {
        if ((bits & SESSION_BIT(i)) && g_sessions[i].in_use)
            g_sessions[i].dirty = true;
    }
}

void SessionReapDead(void)
{
    EventWaitResult d = FormatToEventWait(WaitOn(g_death, TIMEOUT_POLL));
    if (d.status != ZUZU_OK)
        return;
    for (int slot = 0; slot < SESSION_MAX; slot++) {
        Session *s = &g_sessions[slot];
        if (!(d.bits & (1U << slot)) || !s->in_use || s->live < 0)
            continue;
        SvcResult q = HandleQuery(s->live, QUERY_STATUS);
        if (q.r0 != ZUZU_OK || (Err)q.r1 != ERR_DEAD)
            continue;
        LOG_INFO(LOG_TAG, "session %d: app died", slot);
        ReleaseSession(slot);
    }
}

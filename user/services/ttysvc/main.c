#include <dev/protocols/tty.h>
#include <string.h>
#include <util/msg.h>
#include <zuzu/err.h>
#include <zuzu/service.h>
#include <zuzu/udbg.h>
#include <zuzu/zuzu.h>

#define MAX_SESSIONS 30 /* event bits 1..30; bit 0 is the port */
#define MAX_ENDPOINTS 8
#define LINE_MAX_LEN 256
#define PORT_BIT 0
#define SESSION_BIT(slot) ((uint32_t)(slot) + 1U)
#define MASK(bit) (1U << (bit))
#define SESSION_INDEX(gen, slot) ((((uint32_t)(gen)) << 8) | ((uint32_t)(slot) & 0xFFu))
#define INDEX_SLOT(index) ((uint32_t)(index) & 0xFFu)
#define INDEX_GEN(index) ((uint32_t)(index) >> 8)
#define POLL_MS 10 /* safety net: bits are hints, rings are the truth */

/* Peers can write anywhere in the shared page, so none of ttysvc's decisions
 * trust a header field the peer could have scribbled on: masks use
 * TTY_RING_DATA_SIZE, and the indices ttysvc owns (up.tail, down.head) are
 * shadowed locally and only published. */
typedef struct {
    bool in_use;
    bool provider;
    int endpoint;
    uint32_t order; /* attach order, for foreground promotion */
    TtyShm *shm;
    Handle mem;
    Handle peer_doorbell;
    Handle live; /* the peer's liveness port; ERR_DEAD on it means the peer died */
    uint32_t peer_bit;
    bool dirty; /* peer needs a kick */
    uint32_t up_tail;
    uint32_t down_head;
    uint32_t eof_seq;
    uint32_t mode;
    char line[LINE_MAX_LEN];
    uint32_t line_len;
} Session;

typedef struct {
    bool in_use;
    char alias[TTY_NAME_MAX];
    int provider; /* session slot, or -1 while the provider is away */
    int fg;       /* session slot of the foreground consumer, or -1 */
} Endpoint;

static Session g_sessions[MAX_SESSIONS];
static Endpoint g_endpoints[MAX_ENDPOINTS];
static Handle g_port = -1;
static Handle g_event = -1;
/* Liveness ports are bound here, bit = session slot. Kept apart from g_event
 * because peers ring their session's bit on g_event, and the kernel refuses
 * user Signal() on a bit a binding has claimed. */
static Handle g_death = -1;
static uint32_t g_gen[MAX_SESSIONS];
static uint32_t g_order;
static uint32_t g_auto_count;
static uint32_t g_kick_failures;

/* ---- hardened ring access ---- */

static uint32_t UpAvail(const Session *s)
{
    uint32_t n = s->shm->up_hdr.head - s->up_tail;
    return n > TTY_RING_DATA_SIZE ? TTY_RING_DATA_SIZE : n;
}

static uint32_t DownFree(const Session *s)
{
    uint32_t used = s->down_head - s->shm->down_hdr.tail;
    return used > TTY_RING_DATA_SIZE ? 0 : TTY_RING_DATA_SIZE - used;
}

static uint8_t UpPeek(const Session *s)
{
    return s->shm->up_data[s->up_tail & (TTY_RING_DATA_SIZE - 1)];
}

static void UpDrop(Session *s)
{
    s->up_tail++;
    s->shm->up_hdr.tail = s->up_tail;
    s->dirty = true;
}

static void DownPush(Session *s, uint8_t b)
{
    s->shm->down_data[s->down_head & (TTY_RING_DATA_SIZE - 1)] = b;
    s->down_head++;
    s->shm->down_hdr.head = s->down_head;
    s->dirty = true;
}

static void DownPushStr(Session *s, const char *str)
{
    while (*str)
        DownPush(s, (uint8_t)*str++);
}

/* ---- replies ---- */

static void ReplyStatus(Err status)
{
    memcpy(MessageBox(), &status, sizeof(status));
    Reply(sizeof(status), -1);
}

/* ---- endpoints / sessions ---- */

static int FindEndpoint(const char *alias)
{
    for (int i = 0; i < MAX_ENDPOINTS; i++) {
        if (g_endpoints[i].in_use && strcmp(g_endpoints[i].alias, alias) == 0)
            return i;
    }
    return -1;
}

static int NewEndpoint(const char *alias)
{
    for (int i = 0; i < MAX_ENDPOINTS; i++) {
        if (!g_endpoints[i].in_use) {
            g_endpoints[i] = (Endpoint){.in_use = true, .provider = -1, .fg = -1};
            strncpy(g_endpoints[i].alias, alias, TTY_NAME_MAX - 1);
            return i;
        }
    }
    return -1;
}

static void AutoAlias(char out[TTY_NAME_MAX])
{
    for (;; g_auto_count++) {
        out[0] = 't';
        out[1] = 't';
        out[2] = 'y';
        out[3] = (char)('0' + (g_auto_count % 10));
        out[4] = '\0';
        if (FindEndpoint(out) < 0) {
            g_auto_count++;
            return;
        }
    }
}

static int AllocSession(void)
{
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (!g_sessions[i].in_use)
            return i;
    }
    return -1;
}

/* Default attach target: first live endpoint without a foreground consumer,
 * else the first live endpoint (it becomes a background session). */
static int DefaultEndpoint(void)
{
    int first = -1;
    for (int i = 0; i < MAX_ENDPOINTS; i++) {
        if (!g_endpoints[i].in_use || g_endpoints[i].provider < 0)
            continue;
        if (g_endpoints[i].fg < 0)
            return i;
        if (first < 0)
            first = i;
    }
    return first;
}

static void ReleaseSession(int slot)
{
    Session *s = &g_sessions[slot];
    if (s->peer_doorbell >= 0)
        HandleClose(s->peer_doorbell);
    if (s->live >= 0)
        HandleClose(s->live);
    MemUnmap(s->shm);
    HandleClose(s->mem);
    *s = (Session){.peer_doorbell = -1, .live = -1, .mem = -1};
}

static void PromoteForeground(Endpoint *ep)
{
    int best = -1;
    for (int i = 0; i < MAX_SESSIONS; i++) {
        Session *s = &g_sessions[i];
        if (!s->in_use || s->provider || s->endpoint != (int)(ep - g_endpoints))
            continue;
        if (best < 0 || s->order < g_sessions[best].order)
            best = i;
    }
    ep->fg = best;
    if (best >= 0) {
        g_sessions[best].line_len = 0;
        g_sessions[best].dirty = true; /* wake it: it is live now */
    }
}

/* PROVIDE and ATTACH share setup; they differ in how the endpoint is chosen
 * and what the session becomes. */
static void HandleConnect(bool provider, const PortWaitResult *r)
{
    TtyConnectRequest req;
    if (r->granted < 0 || r->xlen < sizeof(req)) {
        if (r->granted >= 0)
            HandleClose(r->granted);
        ReplyStatus(ERR_BADARG);
        return;
    }
    memcpy(&req, MessageBox(), sizeof(req));
    req.alias[TTY_NAME_MAX - 1] = '\0';

    int ep = -1;
    bool new_ep = false;
    Err err = ZUZU_OK;
    if (provider) {
        if (req.alias[0] == '\0')
            AutoAlias(req.alias);
        ep = FindEndpoint(req.alias);
        if (ep >= 0) {
            if (g_endpoints[ep].provider >= 0)
                err = ERR_DUPLICATE; /* alias is live */
        } else {
            ep = NewEndpoint(req.alias);
            new_ep = true;
            if (ep < 0)
                err = ERR_BUSY;
        }
    } else {
        ep = req.alias[0] ? FindEndpoint(req.alias) : DefaultEndpoint();
        if (ep < 0)
            err = ERR_NOENT;
    }
    int slot = err == ZUZU_OK ? AllocSession() : -1;
    if (err == ZUZU_OK && slot < 0)
        err = ERR_BUSY;

    void *va = 0;
    if (err == ZUZU_OK) {
        va = MemMap(r->granted, 0, PROT_RW);
        if (PtrIsErr(va))
            err = (Err)va;
    }
    SvcResult bell = {.r0 = (Register)err};
    if (err == ZUZU_OK)
        bell = HandleDuplicate(g_event, PERM_SEND | PERM_TXFR, MARKER_NONE);
    if (bell.r0 != ZUZU_OK)
        err = (Err)bell.r0;

    if (err != ZUZU_OK) {
        if (va && !PtrIsErr((void *)va))
            MemUnmap(va);
        if (new_ep && ep >= 0)
            g_endpoints[ep].in_use = false;
        HandleClose(r->granted);
        UserspaceDebugLog("ttysvc: %s refused rc=%d", provider ? "provide" : "attach", err);
        ReplyStatus(err);
        return;
    }

    Session *s = &g_sessions[slot];
    *s = (Session){.in_use = true,
                   .provider = provider,
                   .endpoint = ep,
                   .order = g_order++,
                   .shm = (TtyShm *)va,
                   .mem = r->granted,
                   .peer_doorbell = -1,
                   .live = -1};
    g_gen[slot] = (g_gen[slot] + 1) & 0xFFFFFFU;
    if (g_gen[slot] == 0)
        g_gen[slot] = 1;
    memset(s->shm, 0, sizeof(*s->shm));
    ShmRingInit(&s->shm->up_hdr, TTY_RING_DATA_SIZE);
    ShmRingInit(&s->shm->down_hdr, TTY_RING_DATA_SIZE);

    TtyConnectReply rep = {
        .status = ZUZU_OK, .bit = SESSION_BIT(slot), .index = SESSION_INDEX(g_gen[slot], slot)};
    memcpy(MessageBox(), &rep, sizeof(rep));
    Err rc = Reply(sizeof(rep), (Handle)bell.r1);
    HandleClose((Handle)bell.r1);
    if (rc != ZUZU_OK) {
        /* A failed grant already woke the caller with the error. */
        UserspaceDebugLog("ttysvc: reply grant failed rc=%d", rc);
        ReleaseSession(slot);
        if (new_ep)
            g_endpoints[ep].in_use = false;
        return;
    }

    if (provider) {
        g_endpoints[ep].provider = slot;
    } else if (g_endpoints[ep].fg < 0) {
        g_endpoints[ep].fg = slot;
    }
    UserspaceDebugLog("ttysvc: %s slot=%d endpoint=%s%s", provider ? "provide" : "attach", slot,
                      g_endpoints[ep].alias,
                      (!provider && g_endpoints[ep].fg != slot) ? " (background)" : "");
}

static Session *SessionFor(uint32_t index)
{
    uint32_t slot = INDEX_SLOT(index);
    if (slot >= MAX_SESSIONS || !g_sessions[slot].in_use || g_gen[slot] != INDEX_GEN(index))
        return NULL;
    return &g_sessions[slot];
}

static void RouteOutbound(Session *f, Session *p);

static void CloseSession(Session *s, int slot)
{
    Endpoint *ep = &g_endpoints[s->endpoint];
    if (s->provider) {
        ep->provider = -1;
    } else if (ep->fg == slot) {
        if (ep->provider >= 0)
            RouteOutbound(s, &g_sessions[ep->provider]); /* flush its last words */
    }
    bool was_fg = !s->provider && ep->fg == slot;
    ReleaseSession(slot);
    if (was_fg)
        PromoteForeground(ep);
    UserspaceDebugLog("ttysvc: closed slot=%d", slot);
}

static void HandleRequest(const PortWaitResult *r)
{
    uint32_t cmd = 0;
    if (r->xlen >= sizeof(cmd))
        memcpy(&cmd, MessageBox(), sizeof(cmd));
    else {
        if (r->granted >= 0)
            HandleClose(r->granted);
        ReplyStatus(ERR_BADARG);
        return;
    }

    switch (cmd) {
    case TTY_PROVIDE:
        HandleConnect(true, r);
        return;
    case TTY_ATTACH:
        HandleConnect(false, r);
        return;
    case TTY_NOTIFY: {
        TtyNotifyRequest req;
        Session *s = NULL;
        if (r->xlen >= sizeof(req)) {
            memcpy(&req, MessageBox(), sizeof(req));
            s = SessionFor(req.index);
        }
        if (!s || r->granted < 0 || req.bit >= 31) {
            if (r->granted >= 0)
                HandleClose(r->granted);
            ReplyStatus(ERR_BADARG);
            return;
        }
        if (s->peer_doorbell >= 0)
            HandleClose(s->peer_doorbell);
        s->peer_doorbell = r->granted;
        s->peer_bit = req.bit;
        s->dirty = true; /* it may have data waiting already */
        ReplyStatus(ZUZU_OK);
        return;
    }
    case TTY_SETMODE: {
        TtySetModeRequest req;
        Session *s = NULL;
        if (r->xlen >= sizeof(req)) {
            memcpy(&req, MessageBox(), sizeof(req));
            s = SessionFor(req.index);
        }
        if (!s || s->provider || (req.flags & ~TTY_MODE_ALL)) {
            ReplyStatus(ERR_BADARG);
            return;
        }
        s->mode = req.flags;
        s->line_len = 0;
        ReplyStatus(ZUZU_OK);
        return;
    }
    case TTY_CLOSE: {
        TtyCloseRequest req;
        Session *s = NULL;
        if (r->xlen >= sizeof(req)) {
            memcpy(&req, MessageBox(), sizeof(req));
            s = SessionFor(req.index);
        }
        if (!s) {
            ReplyStatus(ERR_NOTCONN);
            return;
        }
        CloseSession(s, (int)INDEX_SLOT(req.index));
        ReplyStatus(ZUZU_OK);
        return;
    }
    case TTY_WATCH: {
        TtyWatchRequest req;
        Session *s = NULL;
        if (r->xlen >= sizeof(req)) {
            memcpy(&req, MessageBox(), sizeof(req));
            s = SessionFor(req.index);
        }
        if (!s || r->granted < 0) {
            if (r->granted >= 0)
                HandleClose(r->granted);
            ReplyStatus(r->granted < 0 && s ? ERR_BADARG : ERR_NOTCONN);
            return;
        }
        if (s->live >= 0) {
            HandleClose(r->granted);
            ReplyStatus(ERR_DUPLICATE);
            return;
        }
        int slot = (int)INDEX_SLOT(req.index);
        Err rc = Bind(EVENT_PORT, g_death, r->granted, (uint32_t)slot);
        if (rc != ZUZU_OK) {
            HandleClose(r->granted);
            if (rc == ERR_DEAD)
                CloseSession(s, slot); /* the peer died before it could be watched */
            ReplyStatus(rc);
            return;
        }
        s->live = r->granted;
        ReplyStatus(ZUZU_OK);
        return;
    }
    default:
        break;
    }
    if (r->granted >= 0)
        HandleClose(r->granted);
    ReplyStatus(ERR_NOSYS);
}

/* ---- routing ---- */

static bool IsPrintable(uint8_t c) { return c >= 0x20 && c < 0x7F; }

/* One typed byte from the provider through the foreground consumer's
 * filter. Returns false if it cannot be handled yet (a ring is full); the
 * byte then stays in the provider's up ring and is retried next pass. */
static bool ProcessInbound(Session *f, Session *p, uint8_t c)
{
    bool echo = (f->mode & TTY_MODE_ECHO) != 0;
    uint32_t pfree = DownFree(p);

    if (!(f->mode & TTY_MODE_COOKED)) {
        if (DownFree(f) < 1 || (echo && pfree < 2))
            return false;
        DownPush(f, c);
        if (echo) {
            if (c == '\r') {
                DownPush(p, '\r');
                DownPush(p, '\n');
            } else {
                DownPush(p, c);
            }
        }
        return true;
    }

    switch (c) {
    case '\r':
    case '\n':
        if (DownFree(f) < f->line_len + 1U || (echo && pfree < 2))
            return false;
        for (uint32_t i = 0; i < f->line_len; i++)
            DownPush(f, (uint8_t)f->line[i]);
        DownPush(f, '\n');
        f->line_len = 0;
        if (echo)
            DownPushStr(p, "\r\n");
        return true;
    case 0x04: /* ^D */
        if (f->line_len == 0 && f->shm->ctl.eof_ack == f->eof_seq) {
            f->shm->ctl.eof_pos = f->down_head;
            f->shm->ctl.eof_seq = ++f->eof_seq;
            f->dirty = true;
        }
        return true; /* ^D mid-line is dropped */
    case 0x03:       /* ^C: flag only, ttysvc never kills anything */
        if (echo && pfree < 4)
            return false;
        f->shm->ctl.intr_seq++;
        f->line_len = 0;
        f->dirty = true;
        if (echo)
            DownPushStr(p, "^C\r\n");
        return true;
    case 0x08:
    case 0x7F:
        if (f->line_len == 0)
            return true;
        if (echo && pfree < 3)
            return false;
        f->line_len--;
        if (echo)
            DownPushStr(p, "\b \b");
        return true;
    case 0x15: /* ^U */
    case 0x17: /* ^W */
    {
        uint32_t keep = 0;
        if (c == 0x17) {
            keep = f->line_len;
            while (keep > 0 && f->line[keep - 1] == ' ')
                keep--;
            while (keep > 0 && f->line[keep - 1] != ' ')
                keep--;
        }
        uint32_t erase = f->line_len - keep;
        if (echo && pfree < 3 * erase)
            return false;
        f->line_len = keep;
        if (echo) {
            for (uint32_t i = 0; i < erase; i++)
                DownPushStr(p, "\b \b");
        }
        return true;
    }
    default:
        if (!IsPrintable(c) || f->line_len >= LINE_MAX_LEN - 1)
            return true;
        if (echo && pfree < 1)
            return false;
        f->line[f->line_len++] = (char)c;
        if (echo)
            DownPush(p, c);
        return true;
    }
}

static void RouteInbound(Session *p, Session *f)
{
    while (UpAvail(p) > 0) {
        if (f && !ProcessInbound(f, p, UpPeek(p)))
            break;
        UpDrop(p); /* with no foreground consumer the byte is discarded */
    }
}

static void RouteOutbound(Session *f, Session *p)
{
    bool cooked = (f->mode & TTY_MODE_COOKED) != 0;
    while (UpAvail(f) > 0) {
        uint8_t c = UpPeek(f);
        bool crlf = cooked && c == '\n';
        if (DownFree(p) < (crlf ? 2U : 1U))
            break;
        if (crlf)
            DownPush(p, '\r');
        DownPush(p, c);
        UpDrop(f);
    }
}

static void ReapDead(void)
{
    EventWaitResult d = FormatToEventWait(WaitOn(g_death, TIMEOUT_POLL));
    if (d.status != ZUZU_OK)
        return;
    for (int slot = 0; slot < MAX_SESSIONS; slot++) {
        Session *s = &g_sessions[slot];
        if (!(d.bits & MASK(slot)) || !s->in_use || s->live < 0)
            continue;
        SvcResult q = HandleQuery(s->live, QUERY_STATUS);
        if (q.r0 != ZUZU_OK || (Err)q.r1 != ERR_DEAD)
            continue;
        UserspaceDebugLog("ttysvc: peer of slot=%d died", slot);
        CloseSession(s, slot);
    }
}

static void ServiceAll(void)
{
    for (int i = 0; i < MAX_ENDPOINTS; i++) {
        Endpoint *ep = &g_endpoints[i];
        if (!ep->in_use || ep->provider < 0)
            continue;
        Session *p = &g_sessions[ep->provider];
        Session *f = ep->fg >= 0 ? &g_sessions[ep->fg] : NULL;
        RouteInbound(p, f);
        if (f)
            RouteOutbound(f, p);
    }

    for (int i = 0; i < MAX_SESSIONS; i++) {
        Session *s = &g_sessions[i];
        if (!s->in_use || !s->dirty || s->peer_doorbell < 0)
            continue;
        s->dirty = false;
        Err rc = Signal(s->peer_doorbell, MASK(s->peer_bit), false);
        if (rc != ZUZU_OK && g_kick_failures++ < 8)
            UserspaceDebugLog("ttysvc: kick slot=%d failed rc=%d", i, rc);
    }
}

int main(void)
{
    UserspaceDebugLog("ttysvc: up");
    for (int i = 0; i < MAX_SESSIONS; i++)
        g_sessions[i] = (Session){.peer_doorbell = -1, .live = -1, .mem = -1};

    g_port = CreatePort();
    g_event = CreateEvent();
    g_death = CreateEvent();
    if (g_port < 0 || g_event < 0 || g_death < 0)
        return ERR_NOMEM;
    Err rc = Bind(EVENT_PORT, g_event, g_port, PORT_BIT);
    if (rc != ZUZU_OK) {
        UserspaceDebugLog("ttysvc: bind port failed rc=%d", rc);
        return rc;
    }

    rc = RegisterService("/svc/tty", g_port);
    UserspaceDebugLog("ttysvc: register /svc/tty rc=%d", rc);
    if (rc != ZUZU_OK)
        return rc;

    for (;;) {
        WaitOn(g_event, POLL_MS);
        ReapDead();

        for (;;) {
            PortWaitResult r = FormatToPortWait(WaitOn(g_port, TIMEOUT_POLL));
            if (r.status != ZUZU_OK)
                break;
            HandleRequest(&r);
        }

        ServiceAll();
    }
}

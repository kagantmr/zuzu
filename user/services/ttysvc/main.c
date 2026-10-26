#include <dev/tty.h>
#include <string.h>
#include <zuzu/msg.h>
#include <zuzu/err.h>
#include <zuzu/service.h>
#include <zuzu/udbg.h>
#include <zuzu/zuzu.h>

#define MAX_SESSIONS 15 /* doorbell bits 1..15, death bits 16..30; bit 0 is the port */
#define MAX_ENDPOINTS 8
#define MAX_PROVIDERS 4
#define LINE_MAX_LEN 256
#define PORT_BIT 0
#define SESSION_BIT(slot) ((uint32_t)(slot) + 1U)
#define DEATH_BIT(slot) ((uint32_t)(slot) + 1U + MAX_SESSIONS)
#define MASK(bit) (1U << (bit))
#define SESSION_INDEX(gen, slot) ((((uint32_t)(gen)) << 8) | ((uint32_t)(slot) & 0xFFu))
#define INDEX_SLOT(index) ((uint32_t)(index) & 0xFFu)
#define INDEX_GEN(index) ((uint32_t)(index) >> 8)

/* Peers can write anywhere in the shared page, so none of ttysvc's decisions
 * trust a header field the peer could have scribbled on: masks use
 * TTY_RING_DATA_SIZE, and the indices ttysvc owns (up.tail, down.head) are
 * shadowed locally and only published. */
typedef struct {
    bool in_use;
    bool provider;
    uint32_t dir; /* providers only: TTY_DIR_* */
    bool wants_input; /* has asked for the foreground at least once */
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
    int providers[MAX_PROVIDERS]; /* session slots, -1 for an empty entry */
    int fg;       /* session slot of the foreground consumer, or -1 */
    uint32_t cols; /* window size reported by a provider, 0 while unknown */
    uint32_t rows;
} Endpoint;

static Session g_sessions[MAX_SESSIONS];
static Endpoint g_endpoints[MAX_ENDPOINTS];
static Handle g_port = -1;
/* Bit 0: the port. SESSION_BIT: rung by peers. DEATH_BIT: bound to the peer's
 * liveness port. The ranges stay apart because the kernel refuses a user
 * Signal() on a bit a binding has claimed. */
static Handle g_event = -1;
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

/* ---- replies ---- */

static void ReplyStatus(Err status)
{
    memcpy(GetMessageBox(), &status, sizeof(status));
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
            g_endpoints[i] = (Endpoint){.in_use = true, .fg = -1};
            for (int j = 0; j < MAX_PROVIDERS; j++)
                g_endpoints[i].providers[j] = -1;
            strncpy(g_endpoints[i].alias, alias, TTY_NAME_MAX - 1);
            return i;
        }
    }
    return -1;
}

static int ProviderCount(const Endpoint *ep)
{
    int n = 0;
    for (int i = 0; i < MAX_PROVIDERS; i++) {
        if (ep->providers[i] >= 0)
            n++;
    }
    return n;
}

static bool AddProvider(Endpoint *ep, int slot)
{
    for (int i = 0; i < MAX_PROVIDERS; i++) {
        if (ep->providers[i] < 0) {
            ep->providers[i] = slot;
            return true;
        }
    }
    return false;
}

static void RemoveProvider(Endpoint *ep, int slot)
{
    for (int i = 0; i < MAX_PROVIDERS; i++) {
        if (ep->providers[i] == slot)
            ep->providers[i] = -1;
    }
}

static Session *OutputAt(const Endpoint *ep, int i)
{
    int slot = ep->providers[i];
    if (slot < 0 || !(g_sessions[slot].dir & TTY_DIR_OUT))
        return NULL;
    return &g_sessions[slot];
}

static bool HasOutput(const Endpoint *ep)
{
    for (int i = 0; i < MAX_PROVIDERS; i++) {
        if (OutputAt(ep, i))
            return true;
    }
    return false;
}

/* Room for the slowest output provider: a byte is only taken from a program
 * or the keyboard once every display and UART can take it too. With no output
 * provider there is no room, so output waits for one. */
static uint32_t OutFree(const Endpoint *ep)
{
    uint32_t room = 0;
    bool any = false;
    for (int i = 0; i < MAX_PROVIDERS; i++) {
        Session *o = OutputAt(ep, i);
        if (!o)
            continue;
        uint32_t f = DownFree(o);
        room = any && room < f ? room : f;
        any = true;
    }
    return room;
}

static void OutPush(const Endpoint *ep, uint8_t b)
{
    for (int i = 0; i < MAX_PROVIDERS; i++) {
        Session *o = OutputAt(ep, i);
        if (o)
            DownPush(o, b);
    }
}

static void OutPushStr(const Endpoint *ep, const char *str)
{
    while (*str)
        OutPush(ep, (uint8_t)*str++);
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
        if (!g_endpoints[i].in_use || ProviderCount(&g_endpoints[i]) == 0)
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
        if (!s->in_use || s->provider || !s->wants_input ||
            s->endpoint != (int)(ep - g_endpoints))
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
    memcpy(&req, GetMessageBox(), sizeof(req));
    req.alias[TTY_NAME_MAX - 1] = '\0';

    int ep = -1;
    bool new_ep = false;
    Err err = ZUZU_OK;
    if (provider) {
        if (req.dir & ~TTY_DIR_BOTH)
            err = ERR_BADARG;
        if (req.alias[0] == '\0')
            AutoAlias(req.alias);
        ep = FindEndpoint(req.alias);
        if (err == ZUZU_OK && ep >= 0) {
            if (ProviderCount(&g_endpoints[ep]) >= MAX_PROVIDERS)
                err = ERR_BUSY;
        } else if (err == ZUZU_OK) {
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
                   .dir = provider ? (req.dir ? req.dir : TTY_DIR_BOTH) : 0,
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
    memcpy(GetMessageBox(), &rep, sizeof(rep));
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

    if (provider)
        AddProvider(&g_endpoints[ep], slot);
    UserspaceDebugLog("ttysvc: %s slot=%d endpoint=%s dir=%u", provider ? "provide" : "attach",
                      slot, g_endpoints[ep].alias, (unsigned)s->dir);
}

static Session *SessionFor(uint32_t index)
{
    uint32_t slot = INDEX_SLOT(index);
    if (slot >= MAX_SESSIONS || !g_sessions[slot].in_use || g_gen[slot] != INDEX_GEN(index))
        return NULL;
    return &g_sessions[slot];
}

static void RouteOutbound(Session *f, const Endpoint *ep);

static void CloseSession(Session *s, int slot)
{
    Endpoint *ep = &g_endpoints[s->endpoint];
    if (s->provider) {
        RemoveProvider(ep, slot);
    } else if (HasOutput(ep)) {
        RouteOutbound(s, ep); /* flush its last words */
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
        memcpy(&cmd, GetMessageBox(), sizeof(cmd));
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
            memcpy(&req, GetMessageBox(), sizeof(req));
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
            memcpy(&req, GetMessageBox(), sizeof(req));
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
    case TTY_FOCUS: {
        TtyFocusRequest req;
        Session *s = NULL;
        if (r->xlen >= sizeof(req)) {
            memcpy(&req, GetMessageBox(), sizeof(req));
            s = SessionFor(req.index);
        }
        if (!s || s->provider) {
            ReplyStatus(ERR_BADARG);
            return;
        }
        Endpoint *ep = &g_endpoints[s->endpoint];
        int slot = (int)INDEX_SLOT(req.index);
        s->wants_input = true;
        if (ep->fg != slot) {
            ep->fg = slot;
            s->line_len = 0;
            s->dirty = true;
        }
        ReplyStatus(ZUZU_OK);
        return;
    }
    case TTY_CLOSE: {
        TtyCloseRequest req;
        Session *s = NULL;
        if (r->xlen >= sizeof(req)) {
            memcpy(&req, GetMessageBox(), sizeof(req));
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
            memcpy(&req, GetMessageBox(), sizeof(req));
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
        Err rc = Bind(EVENT_PORT, g_event, r->granted, DEATH_BIT(slot));
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
    case TTY_GETMODE: {
        TtyGetModeRequest req;
        Session *s = NULL;
        if (r->xlen >= sizeof(req)) {
            memcpy(&req, GetMessageBox(), sizeof(req));
            s = SessionFor(req.index);
        }
        TtyModeReply rep = {.status = (s && !s->provider) ? ZUZU_OK : ERR_BADARG,
                            .flags = s ? s->mode : 0};
        memcpy(GetMessageBox(), &rep, sizeof(rep));
        Reply(sizeof(rep), -1);
        return;
    }
    case TTY_SETWINSZ: {
        TtySetWinszRequest req;
        Session *s = NULL;
        if (r->xlen >= sizeof(req)) {
            memcpy(&req, GetMessageBox(), sizeof(req));
            s = SessionFor(req.index);
        }
        if (!s || !s->provider || req.cols == 0 || req.rows == 0 || req.cols > 0xFFFFU ||
            req.rows > 0xFFFFU) {
            ReplyStatus(ERR_BADARG);
            return;
        }
        int epi = s->endpoint;
        Endpoint *ep = &g_endpoints[epi];
        if (ep->cols != req.cols || ep->rows != req.rows) {
            ep->cols = req.cols;
            ep->rows = req.rows;
            for (int i = 0; i < MAX_SESSIONS; i++) {
                Session *c = &g_sessions[i];
                if (c->in_use && !c->provider && c->endpoint == epi) {
                    c->shm->ctl.winch_seq++;
                    c->dirty = true;
                }
            }
        }
        ReplyStatus(ZUZU_OK);
        return;
    }
    case TTY_GETWINSZ: {
        TtyGetWinszRequest req;
        Session *s = NULL;
        if (r->xlen >= sizeof(req)) {
            memcpy(&req, GetMessageBox(), sizeof(req));
            s = SessionFor(req.index);
        }
        TtyWinszReply rep = {.status = ERR_BADARG};
        if (s) {
            const Endpoint *ep = &g_endpoints[s->endpoint];
            rep.status = (ep->cols && ep->rows) ? ZUZU_OK : ERR_NOENT;
            rep.cols = ep->cols;
            rep.rows = ep->rows;
        }
        memcpy(GetMessageBox(), &rep, sizeof(rep));
        Reply(sizeof(rep), -1);
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

static bool OnlcrOn(uint32_t mode) { return (mode & TTY_OPOST) && (mode & TTY_ONLCR); }

static uint32_t EchoCost(uint32_t mode, uint8_t c) { return (c == '\n' && OnlcrOn(mode)) ? 2U : 1U; }

static void EchoByte(const Endpoint *ep, uint32_t mode, uint8_t c)
{
    if (c == '\n' && OnlcrOn(mode))
        OutPush(ep, '\r');
    OutPush(ep, c);
}

/* One typed byte from an input provider through the foreground consumer's
 * filter. Returns false if it cannot be handled yet (a ring is full); the
 * byte then stays in the provider's up ring and is retried next pass. */
static bool ProcessInboundOne(Session *f, const Endpoint *ep, uint8_t c)
{
    uint32_t m = f->mode;
    bool echo = (m & TTY_ECHO) != 0 && HasOutput(ep);
    uint32_t room = OutFree(ep);

    if ((m & TTY_ICRNL) && c == '\r')
        c = '\n';

    if ((m & TTY_ISIG) && c == 0x03) { /* ^C: flag only, ttysvc never kills anything */
        if (echo && room < 4)
            return false;
        f->shm->ctl.intr_seq++;
        f->line_len = 0;
        f->dirty = true;
        if (echo) {
            OutPush(ep, '^');
            OutPush(ep, 'C');
            EchoByte(ep, m, '\n');
        }
        return true;
    }

    if (!(m & TTY_ICANON)) {
        if (DownFree(f) < 1 || (echo && room < EchoCost(m, c)))
            return false;
        DownPush(f, c);
        if (echo)
            EchoByte(ep, m, c);
        return true;
    }

    switch (c) {
    case '\n':
        if (DownFree(f) < f->line_len + 1U || (echo && room < EchoCost(m, '\n')))
            return false;
        for (uint32_t i = 0; i < f->line_len; i++)
            DownPush(f, (uint8_t)f->line[i]);
        DownPush(f, '\n');
        f->line_len = 0;
        if (echo)
            EchoByte(ep, m, '\n');
        return true;
    case 0x04: /* ^D */
        if (f->line_len == 0 && f->shm->ctl.eof_ack == f->eof_seq) {
            f->shm->ctl.eof_pos = f->down_head;
            f->shm->ctl.eof_seq = ++f->eof_seq;
            f->dirty = true;
        }
        return true; /* ^D mid-line is dropped */
    case 0x08:
    case 0x7F:
        if (f->line_len == 0)
            return true;
        if (echo && room < 3)
            return false;
        f->line_len--;
        if (echo)
            OutPushStr(ep, "\b \b");
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
        if (echo && room < 3 * erase)
            return false;
        f->line_len = keep;
        if (echo) {
            for (uint32_t i = 0; i < erase; i++)
                OutPushStr(ep, "\b \b");
        }
        return true;
    }
    default:
        if (!IsPrintable(c) || f->line_len >= LINE_MAX_LEN - 1)
            return true;
        if (echo && room < 1)
            return false;
        f->line[f->line_len++] = (char)c;
        if (echo)
            OutPush(ep, c);
        return true;
    }
}

/* ^C reaches every other consumer of the endpoint that has ISIG set, so a
 * program that is running (and not the one reading) is interrupted too. */
static void InterruptOthers(const Endpoint *ep, const Session *fg)
{
    int epi = (int)(ep - g_endpoints);
    for (int i = 0; i < MAX_SESSIONS; i++) {
        Session *s = &g_sessions[i];
        if (!s->in_use || s->provider || s->endpoint != epi || s == fg || !(s->mode & TTY_ISIG))
            continue;
        s->shm->ctl.intr_seq++;
        s->line_len = 0;
        s->dirty = true;
    }
}

static bool ProcessInbound(Session *f, const Endpoint *ep, uint8_t c)
{
    bool ok = ProcessInboundOne(f, ep, c);
    if (ok && c == 0x03)
        InterruptOthers(ep, f);
    return ok;
}

static void RouteInbound(Session *p, Session *f, const Endpoint *ep)
{
    while (UpAvail(p) > 0) {
        if (f && !ProcessInbound(f, ep, UpPeek(p)))
            break;
        UpDrop(p); /* with no foreground consumer the byte is discarded */
    }
}

static void RouteOutbound(Session *f, const Endpoint *ep)
{
    bool onlcr = OnlcrOn(f->mode);
    while (UpAvail(f) > 0) {
        uint8_t c = UpPeek(f);
        bool crlf = onlcr && c == '\n';
        if (OutFree(ep) < (crlf ? 2U : 1U))
            break;
        if (crlf)
            OutPush(ep, '\r');
        OutPush(ep, c);
        UpDrop(f);
    }
}

static void ReapDead(EventWord bits)
{
    for (int slot = 0; slot < MAX_SESSIONS; slot++) {
        Session *s = &g_sessions[slot];
        if (!(bits & MASK(DEATH_BIT(slot))) || !s->in_use || s->live < 0)
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
        if (!ep->in_use || ProviderCount(ep) == 0)
            continue;
        Session *f = ep->fg >= 0 ? &g_sessions[ep->fg] : NULL;
        for (int k = 0; k < MAX_PROVIDERS; k++) {
            int slot = ep->providers[k];
            if (slot >= 0 && (g_sessions[slot].dir & TTY_DIR_IN))
                RouteInbound(&g_sessions[slot], f, ep);
        }
        for (int j = 0; j < MAX_SESSIONS; j++) {
            Session *c = &g_sessions[j];
            if (c->in_use && !c->provider && c->endpoint == i)
                RouteOutbound(c, ep);
        }
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
    if (g_port < 0 || g_event < 0)
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
        EventWaitResult w = FormatToEventWait(WaitOn(g_event, TIMEOUT_INFINITE));
        if (w.status == ZUZU_OK)
            ReapDead(w.bits);

        for (;;) {
            PortWaitResult r = FormatToPortWait(WaitOn(g_port, TIMEOUT_POLL));
            if (r.status != ZUZU_OK)
                break;
            HandleRequest(&r);
        }

        ServiceAll();
    }
}

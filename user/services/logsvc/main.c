#define UDBG_ENABLED 1
#include <dev/log.h>
#include <string.h>
#include <zuzu/err.h>
#include <zuzu/msg.h>
#include <zuzu/service.h>
#include <zuzu/udbg.h>
#include <zuzu/uptime.h>
#include <zuzu/zuzu.h>

#define MAX_SESSIONS LOG_MAX_SESSIONS /* doorbell bits 1..15; bit 0 is the port */

#define PORT_BIT 0
/** @brief Parent event bit that means "session @p slot's pipe changed". */
#define DOORBELL_BIT(slot) ((uint32_t)(slot) + 1U)
/** @brief Mask for one event bit. */
#define MASK(bit) (1U << (bit))
/** @brief Both bits a pipe's event can raise: data written, room made. */
#define PIPE_BITS (MASK(PIPE_DEFAULT_DATA_BIT) | MASK(PIPE_DEFAULT_SPACE_BIT))

/** @brief Client id stored on records that arrive through LOG_CRASH. */
#define CLIENT_CRASH 0xFFU
/** @brief Console mirror level that means "off". */
#define CONSOLE_OFF (LOG_LEVEL_FATAL + 1)
/** @brief Marker on the admin handle. Slot byte 0xFF is never a session. */
#define MARKER_ADMIN 0xFFU
/** @brief Marker on a session handle: a generation, then the slot. Never zero or MARKER_ADMIN. */
#define SESSION_MARKER(gen, slot) ((((gen) & 0xFFFFFFU) << 8) | ((uint32_t)(slot) & 0xFFU))
/** @brief Slot a session marker names. */
#define MARKER_SLOT(m) ((m) & 0xFFU)
/** @brief Sources with their own minimum level. */
#define MAX_SOURCE_LEVELS 8

/** @brief A byte ring that holds whole records and evicts the oldest first. */
typedef struct {
    uint8_t *buf;   /**< Storage. */
    uint32_t size;  /**< Bytes in @c buf. */
    uint32_t start; /**< Offset in @c buf of the oldest record. */
    uint32_t used;  /**< Bytes taken by records. */
    uint32_t count; /**< Records held. */
    uint64_t base;  /**< Absolute position of the oldest record; grows as records are evicted. */
} Ring;

/** @brief A session: a writer's pipe logsvc reads, or a reader's pipe logsvc writes. */
typedef struct {
    bool in_use;                 /**< The slot is taken. */
    bool open;                   /**< @c p is open (the event has arrived). */
    uint8_t kind;                /**< LOG_KIND_WRITER or LOG_KIND_READER. */
    Marker marker;               /**< Marker on this session's handle. */
    Handle mem;                  /**< The pipe's page until OpenPipe() takes it, else -1. */
    PipeEnd p;                   /**< Our end of the pipe. */
    uint64_t last_us;            /**< When the session last did something. */
    char source[LOG_SOURCE_MAX]; /**< Writer: default tag from LOG_ATTACH. */
    bool reading;                /**< Reader: LOG_READ has been received. */
    bool finished;               /**< Reader: everything wanted was sent, or the pipe is gone. */
    bool follow;                 /**< Reader: keep going when caught up. */
    bool filter_source;          /**< Reader: only records from @c filter. */
    uint8_t min_level;           /**< Reader: skip records below this. */
    uint32_t cursor;             /**< Reader: next sequence number wanted. */
    uint64_t hist_pos;           /**< Reader: where to resume in the history. */
    uint64_t crash_pos;          /**< Reader: where to resume in the crash history. */
    char filter[LOG_SOURCE_MAX]; /**< Reader: the source it wants. */
} Client;

/** @brief A source's own minimum level. */
typedef struct {
    bool used;                   /**< The entry is in use. */
    uint8_t level;               /**< Records from the source below this are dropped. */
    char source[LOG_SOURCE_MAX]; /**< The source. */
} SourceLevel;

/** @brief A record found in a ring. */
typedef struct {
    bool ok;       /**< A record was found. */
    uint64_t pos;  /**< Its absolute position in the ring. */
    LogEntryHdr h; /**< Its header. */
} Found;

static Handle g_port = -1, g_event = -1;
static Client g_clients[MAX_SESSIONS];
static uint8_t g_hist_buf[LOG_HISTORY_SIZE];
static uint8_t g_crash_buf[LOG_CRASH_SIZE];
static Ring g_hist = {.buf = g_hist_buf, .size = sizeof(g_hist_buf)};
static Ring g_crash = {.buf = g_crash_buf, .size = sizeof(g_crash_buf)};
static SourceLevel g_src_levels[MAX_SOURCE_LEVELS];
static uint32_t g_gen[MAX_SESSIONS];
static bool g_admin_claimed;
static uint32_t g_seq;
static uint32_t g_malformed;
static uint32_t g_dropped_closed;
static uint32_t g_console_level = LOG_LEVEL_INFO;

/** @brief Reply with just a status word. */
static void ReplyStatus(Err status)
{
    memcpy(GetMessageBox(), &status, sizeof(status));
    Reply(sizeof(status), -1);
}

/**
 * @brief Whether sequence number @p a comes before @p b, allowing for wraparound.
 * @param[in] a A sequence number.
 * @param[in] b Another.
 * @return true if @p a is earlier.
 */
static bool SeqLess(uint32_t a, uint32_t b) { return (int32_t)(a - b) < 0; }

/* ---- rings ---- */

/**
 * @brief Copy bytes into a ring, wrapping.
 * @param[in] r   The ring.
 * @param[in] off Offset from the oldest record.
 * @param[in] src Bytes to copy.
 * @param[in] n   How many.
 */
static void RingPut(const Ring *r, uint32_t off, const void *src, uint32_t n)
{
    uint32_t at = (r->start + off) % r->size;
    uint32_t first = r->size - at < n ? r->size - at : n;
    memcpy(r->buf + at, src, first);
    memcpy(r->buf, (const uint8_t *)src + first, n - first);
}

/**
 * @brief Copy bytes out of a ring, wrapping.
 * @param[in]  r   The ring.
 * @param[in]  off Offset from the oldest record.
 * @param[out] dst Destination.
 * @param[in]  n   How many.
 */
static void RingGet(const Ring *r, uint32_t off, void *dst, uint32_t n)
{
    uint32_t at = (r->start + off) % r->size;
    uint32_t first = r->size - at < n ? r->size - at : n;
    memcpy(dst, r->buf + at, first);
    memcpy((uint8_t *)dst + first, r->buf, n - first);
}

/**
 * @brief Drop the oldest record of a ring.
 * @param[in] r The ring, not empty.
 */
static void RingEvict(Ring *r)
{
    LogEntryHdr h;
    RingGet(r, 0, &h, sizeof(h));
    uint32_t size = (uint32_t)LOG_ENTRY_SIZE(h.length);
    r->start = (r->start + size) % r->size;
    r->used -= size;
    r->count--;
    r->base += size;
}

/**
 * @brief Store a record, evicting the oldest records until it fits.
 * @param[in] r   The ring.
 * @param[in] h   The record's header.
 * @param[in] msg The message, @p h->length bytes.
 */
static void RingAppend(Ring *r, const LogEntryHdr *h, const uint8_t *msg)
{
    uint32_t size = (uint32_t)LOG_ENTRY_SIZE(h->length);
    if (size > r->size)
        return;
    while (r->size - r->used < size)
        RingEvict(r);

    static const uint8_t zeros[8];
    RingPut(r, r->used, h, sizeof(*h));
    RingPut(r, r->used + (uint32_t)sizeof(*h), msg, h->length);
    uint32_t tail = size - (uint32_t)sizeof(*h) - h->length;
    RingPut(r, r->used + (uint32_t)sizeof(*h) + h->length, zeros, tail);
    r->used += size;
    r->count++;
}

/**
 * @brief Find the first record at or after a position whose sequence number is at least @p seq.
 * @param[in]     r   The ring.
 * @param[in,out] pos Where to start; moved past records that are too old.
 * @param[in]     seq First sequence number wanted.
 * @return The record, with ok false if there is none.
 */
static Found RingFind(const Ring *r, uint64_t *pos, uint32_t seq)
{
    Found f = {0};
    if (*pos < r->base)
        *pos = r->base;
    uint32_t off = (uint32_t)(*pos - r->base);
    while (off < r->used) {
        RingGet(r, off, &f.h, sizeof(f.h));
        if (!SeqLess(f.h.seq, seq)) {
            f.ok = true;
            f.pos = r->base + off;
            return f;
        }
        off += (uint32_t)LOG_ENTRY_SIZE(f.h.length);
        *pos = r->base + off;
    }
    f.ok = false;
    return f;
}

/* ---- sessions ---- */

/**
 * @brief Find the session a handle belongs to, from the marker on the call.
 * @param[in] marker The call's sender marker.
 * @return The session, or NULL if the marker names no live session: a stale handle, or none.
 */
static Client *ClientFor(Marker marker)
{
    uint32_t slot = MARKER_SLOT(marker);
    if (slot >= MAX_SESSIONS || !g_clients[slot].in_use || g_clients[slot].marker != marker)
        return NULL;
    return &g_clients[slot];
}

/**
 * @brief The slot a session occupies.
 * @param[in] c A session.
 * @return Its index in the table.
 */
static int SlotOf(const Client *c) { return (int)(c - g_clients); }

/**
 * @brief Free a session and everything it holds.
 * @param[in] slot Session slot.
 */
static void ReleaseClient(int slot)
{
    Client *c = &g_clients[slot];
    if (c->open) {
        if (c->kind == LOG_KIND_WRITER)
            g_dropped_closed += PipeDropped(&c->p);
        ClosePipe(&c->p);
    } else if (c->mem >= 0) {
        HandleClose(c->mem);
    }
    memset(c, 0, sizeof(*c));
    c->mem = -1;
}

/**
 * @brief Whether a session can be thrown out to make room.
 * @param[in] c A session in use.
 * @return true if nothing would be lost but a quiet client's slot.
 */
static bool Evictable(const Client *c)
{
    if (c->kind == LOG_KIND_READER)
        return c->finished || !c->open || PipePeerClosed(&c->p);
    return !c->open || PipeAvail(&c->p) == 0;
}

/**
 * @brief Find a free slot, evicting the longest-idle session if the table is full.
 *
 * Without a handle to a client's space there is no way to see it die, so its slot would leak. An
 * evicted writer finds out on its next write (ERR_DEAD) and connects again.
 *
 * @return A free slot, or -1 if every session is busy.
 */
static int AllocSlot(void)
{
    int victim = -1;
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (!g_clients[i].in_use)
            return i;
        if (Evictable(&g_clients[i]) &&
            (victim < 0 || g_clients[i].last_us < g_clients[victim].last_us))
            victim = i;
    }
    if (victim >= 0)
        ReleaseClient(victim);
    return victim;
}

/* ---- records ---- */

/**
 * @brief Make a source tag comparable: printable bytes up to the first NUL, zero after.
 * @param[out] dst The cleaned tag.
 * @param[in]  src The tag as a client sent it.
 */
static void CleanSource(char dst[LOG_SOURCE_MAX], const char src[LOG_SOURCE_MAX])
{
    bool end = false;
    for (int i = 0; i < LOG_SOURCE_MAX; i++) {
        unsigned char ch = (unsigned char)src[i];
        if (end || ch == '\0') {
            end = true;
            dst[i] = '\0';
        } else {
            dst[i] = (ch >= 0x20 && ch < 0x7F) ? (char)ch : '?';
        }
    }
}

/**
 * @brief The lowest level a source may log at.
 * @param[in] source A cleaned tag.
 * @return Its own minimum, or 0.
 */
static uint8_t SourceMin(const char source[LOG_SOURCE_MAX])
{
    for (int i = 0; i < MAX_SOURCE_LEVELS; i++) {
        if (g_src_levels[i].used && memcmp(g_src_levels[i].source, source, LOG_SOURCE_MAX) == 0)
            return g_src_levels[i].level;
    }
    return 0;
}

/**
 * @brief Print a record on the console when it is at or above the mirror level.
 * @param[in] e   The record's header.
 * @param[in] msg The message.
 */
static void Mirror(const LogEntryHdr *e, const uint8_t *msg)
{
    static const char *const names[] = {"TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL"};
    if (e->level < g_console_level)
        return;
    char src[LOG_SOURCE_MAX + 1];
    memcpy(src, e->source, LOG_SOURCE_MAX);
    src[LOG_SOURCE_MAX] = '\0';
    char text[LOG_MSG_MAX + 1];
    memcpy(text, msg, e->length);
    text[e->length] = '\0';
    UserspaceDebugLog("[%6u %-5s] (%s) %s", (unsigned)(e->timestamp_us / 1000U), names[e->level],
                      src, text);
}

/**
 * @brief Stamp a validated record and store it.
 * @param[in] client Session the bytes came from, or CLIENT_CRASH.
 * @param[in] h      The client's header.
 * @param[in] msg    The message, @p h->length bytes.
 * @param[in] ring   Where to keep it.
 */
static void Store(uint8_t client, const LogWireHdr *h, const uint8_t *msg, Ring *ring)
{
    LogEntryHdr e = {.timestamp_us = UptimeUs(),
                     .seq = g_seq,
                     .length = h->length,
                     .level = h->level,
                     .client = client};
    CleanSource(e.source, h->source);
    if (ring == &g_hist && e.level < SourceMin(e.source))
        return;
    g_seq++;
    RingAppend(ring, &e, msg);
    Mirror(&e, msg);
}

/**
 * @brief Read exactly @p n bytes already known to be in the ring.
 * @param[in]  r   The reading end.
 * @param[out] dst Destination.
 * @param[in]  n   Bytes to read.
 * @return true if all @p n bytes were read.
 */
static bool ReadExact(PipeEnd *r, void *dst, size_t n)
{
    uint8_t *p = dst;
    while (n > 0) {
        int got = PipeRead(r, p, n, TIMEOUT_POLL);
        if (got <= 0)
            return false;
        p += got;
        n -= (size_t)got;
    }
    return true;
}

/**
 * @brief Throw away everything pending in a session's ring.
 * @param[in] c The session.
 */
static void DiscardPending(Client *c)
{
    uint8_t scratch[64];
    while (PipeAvail(&c->p) > 0) {
        if (PipeRead(&c->p, scratch, sizeof(scratch), TIMEOUT_POLL) <= 0)
            break;
    }
}

/**
 * @brief Read and validate every record a client has published.
 *
 * A published record is whole, so a bad header or a short body means the stream is out of sync or
 * hostile: everything pending is discarded and counted as one malformed event.
 *
 * @param[in] slot Session slot.
 */
static void DrainClient(int slot)
{
    Client *c = &g_clients[slot];
    if (!c->in_use || !c->open || c->kind != LOG_KIND_WRITER)
        return;

    for (;;) {
        uint32_t avail = PipeAvail(&c->p);
        if (avail == 0)
            return;
        c->last_us = UptimeUs();

        LogWireHdr h;
        if (avail < sizeof(h) || !ReadExact(&c->p, &h, sizeof(h)))
            goto bad;
        if (h.level > LOG_LEVEL_FATAL || h.length > LOG_MSG_MAX)
            goto bad;

        size_t body = LOG_WIRE_SIZE(h.length) - sizeof(h);
        if (PipeAvail(&c->p) < body)
            goto bad;
        uint8_t buf[LOG_WIRE_SIZE(LOG_MSG_MAX) - sizeof(LogWireHdr)];
        if (!ReadExact(&c->p, buf, body))
            goto bad;

        if (h.source[0] == '\0')
            memcpy(h.source, c->source, LOG_SOURCE_MAX);
        Store((uint8_t)slot, &h, buf, &g_hist);
    }

bad:
    g_malformed++;
    DiscardPending(c);
}

/**
 * @brief Send a reader every record it still wants that fits in its pipe.
 * @param[in] slot Session slot of a reader.
 */
static void ServeReader(int slot)
{
    Client *c = &g_clients[slot];
    if (!c->in_use || !c->open || c->kind != LOG_KIND_READER || !c->reading || c->finished)
        return;

    for (;;) {
        Found a = RingFind(&g_hist, &c->hist_pos, c->cursor);
        Found b = RingFind(&g_crash, &c->crash_pos, c->cursor);
        bool use_crash = b.ok && (!a.ok || SeqLess(b.h.seq, a.h.seq));
        Found *f = use_crash ? &b : &a;
        const Ring *ring = use_crash ? &g_crash : &g_hist;
        uint64_t *pos = use_crash ? &c->crash_pos : &c->hist_pos;

        if (!f->ok) {
            if (!c->follow) {
                ClosePipe(&c->p);
                c->open = false;
                c->finished = true;
            }
            return;
        }

        uint32_t size = (uint32_t)LOG_ENTRY_SIZE(f->h.length);
        bool wanted = f->h.level >= c->min_level &&
                      (!c->filter_source || memcmp(f->h.source, c->filter, LOG_SOURCE_MAX) == 0);
        if (wanted) {
            if (PipeFree(&c->p) < size)
                return;
            uint8_t buf[LOG_ENTRY_SIZE(LOG_MSG_MAX)];
            RingGet(ring, (uint32_t)(f->pos - ring->base), buf, size);
            Err rc = PipeTryWriteAll(&c->p, buf, size);
            if (rc == ERR_DEAD) {
                c->finished = true;
                return;
            }
            if (rc != ZUZU_OK)
                return;
        }
        c->cursor = f->h.seq + 1;
        *pos = f->pos + size;
    }
}

/**
 * @brief Drain every writer and serve every reader. Clears each pipe's own event first so its
 * doorbell can fire again.
 */
static void ServiceAll(void)
{
    for (int i = 0; i < MAX_SESSIONS; i++) {
        Client *c = &g_clients[i];
        if (c->in_use && c->open)
            WaitOnEvent(c->p.event, TIMEOUT_POLL, PIPE_BITS, false);
    }
    for (int i = 0; i < MAX_SESSIONS; i++)
        DrainClient(i);
    for (int i = 0; i < MAX_SESSIONS; i++)
        ServeReader(i);
}

/* ---- requests ---- */

/**
 * @brief Handle LOG_ATTACH: take the pipe's page into a free slot.
 * @param[in] r The call.
 */
static void HandleAttach(const PortWaitResult *r)
{
    LogAttachRequest req;
    if (r->granted < 0 || r->xlen < sizeof(req)) {
        if (r->granted >= 0)
            HandleClose(r->granted);
        ReplyStatus(ERR_BADARG);
        return;
    }
    memcpy(&req, GetMessageBox(), sizeof(req));
    if (req.kind != LOG_KIND_WRITER && req.kind != LOG_KIND_READER) {
        HandleClose(r->granted);
        ReplyStatus(ERR_BADARG);
        return;
    }

    int slot = AllocSlot();
    if (slot < 0) {
        HandleClose(r->granted);
        ReplyStatus(ERR_BUSY);
        return;
    }

    g_gen[slot] = (g_gen[slot] + 1) & 0xFFFFFFU;
    if (g_gen[slot] == 0)
        g_gen[slot] = 1;

    Client *c = &g_clients[slot];
    memset(c, 0, sizeof(*c));
    c->in_use = true;
    c->kind = (uint8_t)req.kind;
    c->mem = r->granted;
    c->marker = SESSION_MARKER(g_gen[slot], slot);
    c->last_us = UptimeUs();
    CleanSource(c->source, req.source);

    SvcResult dup = HandleDuplicate(g_port, PERM_SEND | PERM_TXFR, c->marker);
    if (dup.r0 != ZUZU_OK) {
        ReleaseClient(slot);
        ReplyStatus(dup.r0);
        return;
    }

    LogAttachReply rep = {.status = ZUZU_OK, .index = (uint32_t)slot};
    memcpy(GetMessageBox(), &rep, sizeof(rep));
    Err rc = Reply(sizeof(rep), (Handle)dup.r1);
    HandleClose((Handle)dup.r1);
    if (rc != ZUZU_OK)
        ReleaseClient(slot);
}

/**
 * @brief Handle LOG_EVENT: open the pipe and bind its event to this session's doorbell bit.
 * @param[in] c The session whose handle was used.
 * @param[in] r The call.
 */
static void HandleEvent(Client *c, const PortWaitResult *r)
{
    int slot = SlotOf(c);
    if (c->open || c->mem < 0 || r->granted < 0) {
        if (r->granted >= 0)
            HandleClose(r->granted);
        ReplyStatus(ERR_BADARG);
        return;
    }

    PipeRole role = c->kind == LOG_KIND_READER ? PIPE_WRITER : PIPE_READER;
    Err rc = OpenPipe(&c->p, role, c->mem, r->granted, PIPE_DEFAULT_DATA_BIT,
                      PIPE_DEFAULT_SPACE_BIT);
    if (rc != ZUZU_OK) {
        HandleClose(r->granted);
        ReleaseClient(slot);
        ReplyStatus(rc);
        return;
    }
    c->mem = -1;
    c->open = true;

    rc = Bind(EVENT_EVENT, g_event, c->p.event, DOORBELL_BIT(slot));
    if (rc != ZUZU_OK) {
        ReleaseClient(slot);
        ReplyStatus(rc);
        return;
    }
    ReplyStatus(ZUZU_OK);
}

/**
 * @brief Handle LOG_CLOSE.
 * @param[in] c The session whose handle was used.
 */
static void HandleCloseSession(Client *c)
{
    DrainClient(SlotOf(c));
    ReleaseClient(SlotOf(c));
    ReplyStatus(ZUZU_OK);
}

/**
 * @brief Handle LOG_READ: say what a reader wants.
 * @param[in] c The session whose handle was used.
 * @param[in] r The call.
 */
static void HandleRead(Client *c, const PortWaitResult *r)
{
    LogReadRequest req;
    if (r->xlen < sizeof(req)) {
        ReplyStatus(ERR_BADARG);
        return;
    }
    memcpy(&req, GetMessageBox(), sizeof(req));
    if (!c->open || c->kind != LOG_KIND_READER || c->reading || req.min_level > LOG_LEVEL_FATAL ||
        (req.flags & ~LOG_READ_FOLLOW)) {
        ReplyStatus(ERR_BADARG);
        return;
    }
    c->reading = true;
    c->cursor = req.from_seq;
    c->min_level = req.min_level;
    c->follow = (req.flags & LOG_READ_FOLLOW) != 0;
    CleanSource(c->filter, req.source);
    c->filter_source = c->filter[0] != '\0';
    ReplyStatus(ZUZU_OK);
}

/**
 * @brief Handle LOG_SETLEVEL: the console mirror level, or one source's minimum.
 * @param[in] c The session whose handle was used, or NULL for the admin handle.
 * @param[in] r The call.
 */
static void HandleSetLevel(const Client *c, const PortWaitResult *r)
{
    LogLevelRequest req;
    if (r->xlen < sizeof(req)) {
        ReplyStatus(ERR_BADARG);
        return;
    }
    memcpy(&req, GetMessageBox(), sizeof(req));
    if (req.level > CONSOLE_OFF) {
        ReplyStatus(ERR_BADARG);
        return;
    }
    char src[LOG_SOURCE_MAX];
    CleanSource(src, req.source);
    if (src[0] == '\0') {
        if (c) {
            ReplyStatus(ERR_NOPERM);
            return;
        }
        g_console_level = req.level;
        ReplyStatus(ZUZU_OK);
        return;
    }
    if (c && memcmp(src, c->source, LOG_SOURCE_MAX) != 0) {
        ReplyStatus(ERR_NOPERM);
        return;
    }
    if (req.level > LOG_LEVEL_FATAL) {
        ReplyStatus(ERR_BADARG);
        return;
    }

    SourceLevel *slot = NULL;
    for (int i = 0; i < MAX_SOURCE_LEVELS; i++) {
        SourceLevel *s = &g_src_levels[i];
        if (s->used && memcmp(s->source, src, LOG_SOURCE_MAX) == 0) {
            slot = s;
            break;
        }
        if (!s->used && !slot)
            slot = s;
    }
    if (!slot) {
        ReplyStatus(ERR_NOMEM);
        return;
    }
    slot->used = req.level != 0;
    slot->level = (uint8_t)req.level;
    memcpy(slot->source, src, LOG_SOURCE_MAX);
    ReplyStatus(ZUZU_OK);
}

/** @brief Handle LOG_STATS. */
static void HandleStats(void)
{
    LogStatsReply rep = {.status = ZUZU_OK,
                         .next_seq = g_seq,
                         .records = g_hist.count + g_crash.count,
                         .dropped = g_dropped_closed,
                         .malformed = g_malformed,
                         .console_level = g_console_level};
    for (int i = 0; i < MAX_SESSIONS; i++) {
        const Client *c = &g_clients[i];
        if (!c->in_use)
            continue;
        rep.sessions++;
        if (c->open && c->kind == LOG_KIND_WRITER)
            rep.dropped += PipeDropped(&c->p);
    }
    memcpy(GetMessageBox(), &rep, sizeof(rep));
    Reply(sizeof(rep), -1);
}

/**
 * @brief Handle LOG_CRASH: keep a record where debug output cannot evict it.
 * @param[in] r The call.
 */
static void HandleCrash(const PortWaitResult *r)
{
    LogCrashRequest req;
    if (r->xlen < sizeof(req)) {
        ReplyStatus(ERR_BADARG);
        return;
    }
    memcpy(&req, GetMessageBox(), sizeof(req));
    if (req.level > LOG_LEVEL_FATAL || req.length > LOG_CRASH_MSG_MAX ||
        r->xlen < sizeof(req) + req.length) {
        ReplyStatus(ERR_BADARG);
        return;
    }
    LogWireHdr h = {.level = req.level, .length = req.length};
    memcpy(h.source, req.source, LOG_SOURCE_MAX);
    uint8_t msg[LOG_CRASH_MSG_MAX];
    memcpy(msg, (const uint8_t *)GetMessageBox() + sizeof(req), req.length);
    Store(CLIENT_CRASH, &h, msg, &g_crash);
    ReplyStatus(ZUZU_OK);
}

/** @brief Handle LOG_CLAIM: give the first caller the admin handle. */
static void HandleClaim(void)
{
    if (g_admin_claimed) {
        ReplyStatus(ERR_BUSY);
        return;
    }
    SvcResult dup = HandleDuplicate(g_port, PERM_SEND | PERM_TXFR, MARKER_ADMIN);
    if (dup.r0 != ZUZU_OK) {
        ReplyStatus(dup.r0);
        return;
    }
    Err status = ZUZU_OK;
    memcpy(GetMessageBox(), &status, sizeof(status));
    Err rc = Reply(sizeof(status), (Handle)dup.r1);
    HandleClose((Handle)dup.r1);
    if (rc == ZUZU_OK)
        g_admin_claimed = true;
}

/**
 * @brief Dispatch one request, by what the handle it came through allows.
 * @param[in] r The call.
 */
static void HandleRequest(const PortWaitResult *r)
{
    uint32_t cmd = 0;
    if (r->xlen < sizeof(cmd)) {
        if (r->granted >= 0)
            HandleClose(r->granted);
        ReplyStatus(ERR_BADARG);
        return;
    }
    memcpy(&cmd, GetMessageBox(), sizeof(cmd));

    bool is_public = r->sender == MARKER_NONE;
    bool is_admin = r->sender == MARKER_ADMIN;
    Client *c = (is_public || is_admin) ? NULL : ClientFor(r->sender);

    bool known = cmd == LOG_ATTACH || cmd == LOG_EVENT || cmd == LOG_CLOSE || cmd == LOG_READ ||
                 cmd == LOG_SETLEVEL || cmd == LOG_STATS || cmd == LOG_CRASH || cmd == LOG_CLAIM;
    bool allowed = false;
    switch (cmd) {
    case LOG_ATTACH:
    case LOG_CLAIM:
        allowed = is_public;
        break;
    case LOG_EVENT:
    case LOG_CLOSE:
    case LOG_READ:
        allowed = c != NULL;
        break;
    case LOG_SETLEVEL:
        allowed = c != NULL || is_admin;
        break;
    case LOG_CRASH:
        allowed = is_admin;
        break;
    case LOG_STATS:
        allowed = true;
        break;
    default:
        break;
    }
    if (!known || !allowed) {
        if (r->granted >= 0)
            HandleClose(r->granted);
        ReplyStatus(known ? ERR_NOPERM : ERR_NOSYS);
        return;
    }

    switch (cmd) {
    case LOG_ATTACH:
        HandleAttach(r);
        return;
    case LOG_EVENT:
        HandleEvent(c, r);
        return;
    default:
        break;
    }
    if (r->granted >= 0)
        HandleClose(r->granted);
    switch (cmd) {
    case LOG_CLOSE:
        HandleCloseSession(c);
        return;
    case LOG_READ:
        HandleRead(c, r);
        return;
    case LOG_SETLEVEL:
        HandleSetLevel(c, r);
        return;
    case LOG_STATS:
        HandleStats();
        return;
    case LOG_CRASH:
        HandleCrash(r);
        return;
    case LOG_CLAIM:
        HandleClaim();
        return;
    default:
        ReplyStatus(ERR_NOSYS);
        return;
    }
}

int main(void)
{
    for (int i = 0; i < MAX_SESSIONS; i++)
        g_clients[i].mem = -1;

    g_port = CreatePort();
    g_event = CreateEvent();
    if (g_port < 0 || g_event < 0)
        return ERR_NOMEM;
    Err rc = Bind(EVENT_PORT, g_event, g_port, PORT_BIT);
    if (rc != ZUZU_OK)
        return rc;

    rc = RegisterService("/svc/log", g_port);
    if (rc != ZUZU_OK)
        return rc;

    for (;;) {
        (void)WaitOnEvent(g_event, TIMEOUT_INFINITE, 0, false);

        for (;;) {
            PortWaitResult r = WaitOnPort(g_port, TIMEOUT_POLL);
            if (r.status != ZUZU_OK)
                break;
            HandleRequest(&r);
        }

        ServiceAll();
    }
}

/**
 * @file pipe.c
 *
 * @brief Pipes are a one way stream between spaces, over shared memory and an event.
 */

#include <stdbool.h>
#include <string.h>
#include <util/pipe.h>
#include <zuzu/uptime.h>

/** @brief Highest event bit a pipe may use. Bit 31 is reserved by the kernel. */
#define PIPE_BIT_MAX 30u
/** @brief Rights each end of a pipe made by CreatePipe() gets on the page. */
#define PIPE_MEM_PERMS (PERM_MAP | PERM_WRITE | PERM_TXFR)
/** @brief Rights each end of a pipe made by CreatePipe() gets on the event. */
#define PIPE_EVENT_PERMS (PERM_SEND | PERM_WAIT | PERM_TXFR)
/** @brief Largest transfer a single call reports, so the result fits an int. */
#define PIPE_IO_MAX 0x7FFFFFFFu

/**
 * @brief The event word with only @p bit set.
 *
 * @param[in] bit Bit number 0-30.
 * @return The event word with only that bit set.
 */
static inline EventWord Mask(size_t bit) { return (EventWord)1U << bit; }

/**
 * @brief Whether @p size is a legal ring size.
 *
 * @param[in] size Ring size in bytes.
 * @return true if it is a legal ring size.
 */
static bool SizeOk(size_t size)
{
    return size >= PIPE_SIZE_MIN && size <= PIPE_SIZE_MAX && (size & (size - 1)) == 0;
}

/**
 * @brief Whether the two event bits are usable and distinct.
 *
 * @param[in] data_bit Bit for data.
 * @param[in] space_bit Bit for space.
 * @return true if both are usable and distinct.
 */
static bool BitsOk(size_t data_bit, size_t space_bit)
{
    return data_bit <= PIPE_BIT_MAX && space_bit <= PIPE_BIT_MAX && data_bit != space_bit;
}

/**
 * @brief Bytes in the ring that a reader could take.
 *
 * @param[in] e Either end.
 * @return Bytes in the ring that a reader could take.
 */
static uint32_t RingAvail(const PipeEnd *e)
{
    uint32_t head = ShmRingLoadHead(e->ring);
    uint32_t tail = ShmRingLoadTail(e->ring);
    uint32_t n = head - tail;
    return n > e->size ? (uint32_t)e->size : n;
}

/**
 * @brief Bytes of room a writer has.
 *
 * @param[in] e Either end.
 * @return Bytes of room a writer has.
 */
static uint32_t RingFree(const PipeEnd *e) { return (uint32_t)e->size - RingAvail(e); }

/**
 * @brief The ring's closed flags, read with acquire ordering.
 *
 * @param[in] e Either end.
 * @return The ring's PIPE_F_* flags, read with acquire ordering.
 */
static uint32_t LoadFlags(const PipeEnd *e)
{
    return __atomic_load_n(&e->ctl->flags, __ATOMIC_ACQUIRE);
}

/**
 * @brief Signals @p mask on the event the peer waits on.
 *
 * @param[in] e Either end.
 * @param[in] mask Bits to signal.
 */
static void Kick(const PipeEnd *e, EventWord mask) { Signal(e->kick, mask, false); }

/* A timeout that spans several waits: the deadline is fixed when the call starts. */
/** @brief A timeout that spans several waits: the deadline is fixed when the call starts. */
typedef struct {
    Duration timeout;     /**< The caller's timeout. */
    uint64_t deadline_ms; /**< When it runs out, in uptime ms; unused for a poll or infinite wait. */
} Deadline;

/**
 * @brief Starts a deadline @p timeout from now.
 *
 * @param[in] timeout TIMEOUT_POLL, TIMEOUT_INFINITE, or milliseconds.
 * @return The deadline.
 */
static Deadline DeadlineStart(Duration timeout)
{
    Deadline d = {.timeout = timeout, .deadline_ms = 0};
    if (timeout != TIMEOUT_POLL && timeout != TIMEOUT_INFINITE)
        d.deadline_ms = UptimeMs() + timeout;
    return d;
}

/* Sets @p left to what a wait may still last. Returns false once the time is up (always for a
   poll, so a poll never waits). */
/**
 * @brief Sets @p left to what a wait may still last; false once time is up (always for a poll).
 *
 * @param[in] d The deadline.
 * @param[in] left Receives what a wait may still last.
 * @return false once time is up (always for a poll).
 */
static bool DeadlineLeft(const Deadline *d, Duration *left)
{
    if (d->timeout == TIMEOUT_POLL)
        return false;
    if (d->timeout == TIMEOUT_INFINITE) {
        *left = TIMEOUT_INFINITE;
        return true;
    }
    uint64_t now = UptimeMs();
    if (now >= d->deadline_ms)
        return false;
    *left = (Duration)(d->deadline_ms - now);
    return true;
}

/**
 * @brief Resets an end to the closed state.
 *
 * @param[in] e The end to reset.
 */
static void ForgetEnd(PipeEnd *e)
{
    memset(e, 0, sizeof(*e));
    e->mem = -1;
    e->event = -1;
    e->kick = -1;
}

/* Fills in an end of an owned page; the bits say which way each notification goes. */
/**
 * @brief Fills in an end of an owned page; the bits say which way each notification goes.
 *
 * @param[in] e The end.
 * @param[in] role Reader or writer.
 * @param[in] shm The mapped page.
 * @param[in] mem The page's memory object.
 * @param[in] event The shared event.
 * @param[in] size Ring size in bytes.
 * @param[in] data_bit Bit for data.
 * @param[in] space_bit Bit for space.
 */
static void FillEnd(PipeEnd *e, PipeRole role, PipeShm *shm, Handle mem, Handle event,
                    size_t size, size_t data_bit, size_t space_bit)
{
    ForgetEnd(e);
    e->ring = &shm->ring;
    e->ctl = &shm->ctl;
    e->data = (uint8_t *)shm + PIPE_HDR_SIZE;
    e->size = size;
    e->shm = shm;
    e->mem = mem;
    e->event = event;
    e->kick = event;
    e->wait_mask = Mask(role == PIPE_WRITER ? space_bit : data_bit);
    e->kick_mask = Mask(role == PIPE_WRITER ? data_bit : space_bit);
    e->close_mask = Mask(data_bit) | Mask(space_bit);
    e->intr_seen = shm->ctl.intr_seq;
    e->role = role;
}

/* Maps the page and fills in an end. On failure nothing is changed and no handle is touched. */
/**
 * @brief Maps the page and fills in an end; on failure nothing changes and no handle is touched.
 *
 * @param[in] e The end.
 * @param[in] role Reader or writer.
 * @param[in] mem The page's memory object.
 * @param[in] event The shared event.
 * @param[in] size Ring size in bytes.
 * @param[in] data_bit Bit for data.
 * @param[in] space_bit Bit for space.
 * @return ZUZU_OK, or the mapping error.
 */
static Err MapEnd(PipeEnd *e, PipeRole role, Handle mem, Handle event, size_t size, size_t data_bit,
                  size_t space_bit)
{
    void *va = MemMap(mem, 0, PROT_RW);
    if (PtrIsErr(va))
        return (Err)(intptr_t)va;
    FillEnd(e, role, (PipeShm *)va, mem, event, size, data_bit, space_bit);
    return ZUZU_OK;
}

Err CreatePipe(size_t size, PipeEnd *w, PipeEnd *r)
{
    if (!w || !r || !SizeOk(size))
        return ERR_BADARG;

    ForgetEnd(w);
    ForgetEnd(r);

    Handle mem = CreateMem(1 + (size / PIPE_PAGE_SIZE), 0);
    if (mem < 0)
        return (Err)mem;
    Handle event = CreateEvent();
    if (event < 0) {
        HandleClose(mem);
        return (Err)event;
    }

    /* The reader gets its own copies and its own mapping, so closing one end never pulls the
       page out from under the other. */
    Handle mem2 = -1, event2 = -1;
    Err rc = HandleDup(mem, PIPE_MEM_PERMS, MARKER_NONE, &mem2);
    if (rc == ZUZU_OK)
        rc = HandleDup(event, PIPE_EVENT_PERMS, MARKER_NONE, &event2);
    if (rc == ZUZU_OK)
        rc =
            MapEnd(w, PIPE_WRITER, mem, event, size, PIPE_DEFAULT_DATA_BIT, PIPE_DEFAULT_SPACE_BIT);
    if (rc == ZUZU_OK) {
        ShmRingInit(&w->shm->ring, (uint32_t)size);
        memset(&w->shm->ctl, 0, sizeof(w->shm->ctl));
        rc = MapEnd(r, PIPE_READER, mem2, event2, size, PIPE_DEFAULT_DATA_BIT,
                    PIPE_DEFAULT_SPACE_BIT);
        if (rc != ZUZU_OK)
            MemUnmap(w->shm);
    }

    if (rc != ZUZU_OK) {
        if (event2 >= 0)
            HandleClose(event2);
        if (mem2 >= 0)
            HandleClose(mem2);
        HandleClose(event);
        HandleClose(mem);
        ForgetEnd(w);
        ForgetEnd(r);
    }
    return rc;
}

Err OpenPipe(PipeEnd *e, PipeRole role, Handle mem, Handle event, size_t data_bit, size_t space_bit)
{
    if (!e || (role != PIPE_WRITER && role != PIPE_READER) || mem < 0 || event < 0 ||
        !BitsOk(data_bit, space_bit))
        return ERR_BADARG;

    size_t bytes = 0;
    Err rc = HandleQuerySize(mem, &bytes);
    if (rc != ZUZU_OK)
        return rc;
    if (bytes <= PIPE_HDR_SIZE || !SizeOk(bytes - PIPE_HDR_SIZE))
        return ERR_BADARG;

    return MapEnd(e, role, mem, event, bytes - PIPE_HDR_SIZE, data_bit, space_bit);
}

void PipeViewInit(PipeEnd *e, PipeRole role, ShmRingHdr *ring, PipeCtl *ctl, uint8_t *data,
                  size_t size, Handle event, EventWord wait_mask, Handle kick, EventWord kick_mask)
{
    ForgetEnd(e);
    e->ring = ring;
    e->ctl = ctl;
    e->data = data;
    e->size = size;
    e->event = event;
    e->wait_mask = wait_mask;
    e->kick = kick;
    e->kick_mask = kick_mask;
    e->close_mask = kick_mask;
    e->intr_seen = ctl->intr_seq;
    e->role = role;
}

bool PipeInterrupted(const PipeEnd *e) { return e && e->ring && e->ctl->intr_seq != e->intr_seen; }

void PipeAckInterrupt(PipeEnd *e)
{
    if (e && e->ring)
        e->intr_seen = e->ctl->intr_seq;
}

Err PipeDupHandles(const PipeEnd *e, HandlePerms event_perms, Handle *mem, Handle *event)
{
    if (!e || !e->shm || !mem || !event)
        return ERR_BADARG;

    Handle m = -1, ev = -1;
    Err rc = HandleDup(e->mem, PIPE_MEM_PERMS, MARKER_NONE, &m);
    if (rc != ZUZU_OK)
        return rc;
    rc = HandleDup(e->event, event_perms, MARKER_NONE, &ev);
    if (rc != ZUZU_OK) {
        HandleClose(m);
        return rc;
    }
    *mem = m;
    *event = ev;
    return ZUZU_OK;
}

/* Copies up to @p n bytes in and publishes them; returns how many fit. */
/**
 * @brief Copies up to @p n bytes in and publishes them; returns how many fit.
 *
 * @param[in] w The writer.
 * @param[in] src Bytes to send.
 * @param[in] n How many are on offer.
 * @return How many fit.
 */
static size_t WriteSome(PipeEnd *w, const uint8_t *src, size_t n)
{
    uint32_t room = RingFree(w);
    size_t k = n < room ? n : room;
    if (k == 0)
        return 0;
    uint32_t head = ShmRingLoadHead(w->ring);
    ShmRingWriteAt(w->data, (uint32_t)w->size, head, src, (uint32_t)k);
    ShmRingPublishHead(w->ring, head + (uint32_t)k);
    Kick(w, w->kick_mask);
    return k;
}

/* Copies up to @p cap bytes out and publishes the new tail; returns how many were there. */
/**
 * @brief Copies up to @p cap bytes out and publishes the new tail; returns how many were there.
 *
 * @param[in] r The reader.
 * @param[in] dst Where to store the bytes.
 * @param[in] cap Room in @p dst.
 * @return How many were there.
 */
static size_t ReadSome(PipeEnd *r, uint8_t *dst, size_t cap)
{
    uint32_t have = RingAvail(r);
    size_t k = cap < have ? cap : have;
    if (k == 0)
        return 0;
    uint32_t tail = ShmRingLoadTail(r->ring);
    ShmRingReadAt(r->data, (uint32_t)r->size, tail, dst, (uint32_t)k);
    ShmRingPublishTail(r->ring, tail + (uint32_t)k);
    Kick(r, r->kick_mask);
    return k;
}

/* A terminal's ^D: ordered against the data, so it only counts once everything typed before it
   has been read. Consumes it. */
/**
 * @brief Whether a terminal's ^D is due here, ordered after the data; consumes it.
 *
 * @param[in] r The reader.
 * @return true once for each ^D, when everything before it has been read.
 */
static bool EofHere(PipeEnd *r)
{
    PipeCtl *c = r->ctl;
    if (c->eof_seq == c->eof_ack || r->ring->tail != c->eof_pos)
        return false;
    c->eof_ack = c->eof_seq;
    return true;
}

int PipeWrite(PipeEnd *w, const void *buf, size_t len, Duration timeout)
{
    if (!w || !w->ring || w->role != PIPE_WRITER || (!buf && len))
        return ERR_BADARG;
    if (len > PIPE_IO_MAX)
        len = PIPE_IO_MAX;

    const uint8_t *src = buf;
    size_t done = 0;
    Deadline dl = DeadlineStart(timeout);

    while (done < len) {
        if (LoadFlags(w) & PIPE_F_READER_CLOSED)
            return done ? (int)done : ERR_DEAD;

        size_t put = WriteSome(w, src + done, len - done);
        done += put;
        if (done == len)
            break;
        if (put)
            continue; /* room may have opened up while we copied */

        if (PipeInterrupted(w))
            return done ? (int)done : ERR_INTR;

        /* The ring is full: wait for the reader to make room. A stale bit only wakes us
           early, and the loop checks again. */
        Duration left = 0;
        if (!DeadlineLeft(&dl, &left))
            break;
        EventWaitResult res = WaitOnEvent(w->event, left, w->wait_mask, false);
        if (res.status != ZUZU_OK && res.status != ERR_TIMEOUT)
            return done ? (int)done : res.status;
    }
    return done ? (int)done : (len ? ERR_TIMEOUT : 0);
}

Err PipeTryWriteAll(PipeEnd *w, const void *buf, size_t len)
{
    if (!w || !w->ring || w->role != PIPE_WRITER || (!buf && len))
        return ERR_BADARG;
    if (len == 0)
        return ZUZU_OK;
    if (len > w->size)
        return ERR_OVERFLOW; /* it can never fit */
    if (LoadFlags(w) & PIPE_F_READER_CLOSED)
        return ERR_DEAD;

    if (RingFree(w) < len) {
        w->ctl->dropped = w->ctl->dropped + 1;
        return ERR_BUFFULL;
    }
    WriteSome(w, buf, len);
    return ZUZU_OK;
}

int PipeRead(PipeEnd *r, void *buf, size_t cap, Duration timeout)
{
    if (!r || !r->ring || r->role != PIPE_READER || (!buf && cap))
        return ERR_BADARG;
    if (cap == 0)
        return 0;
    if (cap > PIPE_IO_MAX)
        cap = PIPE_IO_MAX;

    Deadline dl = DeadlineStart(timeout);

    for (;;) {
        bool closed = (LoadFlags(r) & PIPE_F_WRITER_CLOSED) != 0;

        size_t got = ReadSome(r, buf, cap);
        if (got)
            return (int)got;
        if (closed || EofHere(r))
            return 0;
        if (PipeInterrupted(r))
            return ERR_INTR;

        Duration left = 0;
        if (!DeadlineLeft(&dl, &left))
            return ERR_TIMEOUT;
        EventWaitResult res = WaitOnEvent(r->event, left, r->wait_mask, false);
        if (res.status != ZUZU_OK && res.status != ERR_TIMEOUT)
            return res.status;
    }
}

uint32_t PipeAvail(const PipeEnd *r) { return (r && r->ring) ? RingAvail(r) : 0; }

uint32_t PipeFree(const PipeEnd *w) { return (w && w->ring) ? RingFree(w) : 0; }

uint32_t PipeDropped(const PipeEnd *e) { return (e && e->ring) ? e->ctl->dropped : 0; }

bool PipePeerClosed(const PipeEnd *e)
{
    if (!e || !e->ring)
        return true;
    uint32_t gone = (e->role == PIPE_WRITER) ? PIPE_F_READER_CLOSED : PIPE_F_WRITER_CLOSED;
    return (LoadFlags(e) & gone) != 0;
}

void PipeMarkClosed(PipeEnd *e, PipeRole which)
{
    if (!e || !e->ring || (which != PIPE_WRITER && which != PIPE_READER))
        return;

    uint32_t flag = (which == PIPE_WRITER) ? PIPE_F_WRITER_CLOSED : PIPE_F_READER_CLOSED;
    __atomic_fetch_or(&e->ctl->flags, flag, __ATOMIC_RELEASE);
    Kick(e, e->close_mask);
}

void PipeDetach(PipeEnd *e)
{
    if (!e || !e->ring)
        return;

    if (e->shm) {
        MemUnmap(e->shm);
        HandleClose(e->mem);
        HandleClose(e->event);
    }
    ForgetEnd(e);
}

void ClosePipe(PipeEnd *e)
{
    if (!e || !e->ring)
        return;

    if (e->shm)
        PipeMarkClosed(e, e->role);
    PipeDetach(e);
}

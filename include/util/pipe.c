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
/** @brief Bits CreatePipe() uses on its own event. */
#define PIPE_DEFAULT_DATA_BIT 0u
#define PIPE_DEFAULT_SPACE_BIT 1u
/** @brief Rights each end of a pipe made by CreatePipe() gets on the page. */
#define PIPE_MEM_PERMS (PERM_MAP | PERM_WRITE | PERM_TXFR)
/** @brief Rights each end of a pipe made by CreatePipe() gets on the event. */
#define PIPE_EVENT_PERMS (PERM_SEND | PERM_WAIT | PERM_TXFR)
/** @brief Largest transfer a single call reports, so the result fits an int. */
#define PIPE_IO_MAX 0x7FFFFFFFu

static inline EventWord Mask(size_t bit) { return (EventWord)1U << bit; }

static bool SizeOk(size_t size)
{
    return size >= PIPE_SIZE_MIN && size <= PIPE_SIZE_MAX && (size & (size - 1)) == 0;
}

static bool BitsOk(size_t data_bit, size_t space_bit)
{
    return data_bit <= PIPE_BIT_MAX && space_bit <= PIPE_BIT_MAX && data_bit != space_bit;
}

static uint32_t RingAvail(const PipeEnd *e)
{
    uint32_t head = ShmRingLoadHead(&e->shm->ring);
    uint32_t tail = ShmRingLoadTail(&e->shm->ring);
    uint32_t n = head - tail;
    return n > e->size ? (uint32_t)e->size : n;
}

static uint32_t RingFree(const PipeEnd *e) { return (uint32_t)e->size - RingAvail(e); }

static uint32_t LoadFlags(const PipeEnd *e)
{
    return __atomic_load_n(&e->shm->flags, __ATOMIC_ACQUIRE);
}

static void Kick(const PipeEnd *e, size_t bit) { Signal(e->event, Mask(bit), false); }

/* A timeout that spans several waits: the deadline is fixed when the call starts. */
typedef struct {
    Duration timeout;
    uint64_t deadline_ms;
} Deadline;

static Deadline DeadlineStart(Duration timeout)
{
    Deadline d = {.timeout = timeout, .deadline_ms = 0};
    if (timeout != TIMEOUT_POLL && timeout != TIMEOUT_INFINITE)
        d.deadline_ms = UptimeMs() + timeout;
    return d;
}

/* Sets @p left to what a wait may still last. Returns false once the time is up (always for a
   poll, so a poll never waits). */
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

/* Maps the page and fills in an end. On failure nothing is changed and no handle is touched. */
static Err MapEnd(PipeEnd *e, PipeRole role, Handle mem, Handle event, size_t size, size_t data_bit,
                  size_t space_bit)
{
    void *va = MemMap(mem, 0, PROT_RW);
    if (PtrIsErr(va))
        return (Err)(intptr_t)va;

    e->shm = (PipeShm *)va;
    e->data = (uint8_t *)va + PIPE_HDR_SIZE;
    e->size = size;
    e->mem = mem;
    e->event = event;
    e->data_bit = data_bit;
    e->space_bit = space_bit;
    e->role = role;
    return ZUZU_OK;
}

static void ForgetEnd(PipeEnd *e)
{
    memset(e, 0, sizeof(*e));
    e->mem = -1;
    e->event = -1;
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
        w->shm->flags = 0;
        w->shm->dropped = 0;
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
static size_t WriteSome(PipeEnd *w, const uint8_t *src, size_t n)
{
    uint32_t room = RingFree(w);
    size_t k = n < room ? n : room;
    if (k == 0)
        return 0;
    uint32_t head = ShmRingLoadHead(&w->shm->ring);
    ShmRingWriteAt(w->data, (uint32_t)w->size, head, src, (uint32_t)k);
    ShmRingPublishHead(&w->shm->ring, head + (uint32_t)k);
    Kick(w, w->data_bit);
    return k;
}

/* Copies up to @p cap bytes out and publishes the new tail; returns how many were there. */
static size_t ReadSome(PipeEnd *r, uint8_t *dst, size_t cap)
{
    uint32_t have = RingAvail(r);
    size_t k = cap < have ? cap : have;
    if (k == 0)
        return 0;
    uint32_t tail = ShmRingLoadTail(&r->shm->ring);
    ShmRingReadAt(r->data, (uint32_t)r->size, tail, dst, (uint32_t)k);
    ShmRingPublishTail(&r->shm->ring, tail + (uint32_t)k);
    Kick(r, r->space_bit);
    return k;
}

int PipeWrite(PipeEnd *w, const void *buf, size_t len, Duration timeout)
{
    if (!w || !w->shm || w->role != PIPE_WRITER || (!buf && len))
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

        /* The ring is full: wait for the reader to make room. A stale space bit only wakes us
           early, and the loop checks again. */
        Duration left = 0;
        if (!DeadlineLeft(&dl, &left))
            break;
        EventWaitResult res = WaitOnEvent(w->event, left, Mask(w->space_bit), false);
        if (res.status != ZUZU_OK && res.status != ERR_TIMEOUT)
            return done ? (int)done : res.status;
    }
    return done ? (int)done : (len ? ERR_TIMEOUT : 0);
}

Err PipeTryWriteAll(PipeEnd *w, const void *buf, size_t len)
{
    if (!w || !w->shm || w->role != PIPE_WRITER || (!buf && len))
        return ERR_BADARG;
    if (len == 0)
        return ZUZU_OK;
    if (len > w->size)
        return ERR_OVERFLOW; /* it can never fit */
    if (LoadFlags(w) & PIPE_F_READER_CLOSED)
        return ERR_DEAD;

    if (RingFree(w) < len) {
        w->shm->dropped = w->shm->dropped + 1;
        return ERR_BUFFULL;
    }
    WriteSome(w, buf, len);
    return ZUZU_OK;
}

int PipeRead(PipeEnd *r, void *buf, size_t cap, Duration timeout)
{
    if (!r || !r->shm || r->role != PIPE_READER || (!buf && cap))
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
        if (closed)
            return 0;

        Duration left = 0;
        if (!DeadlineLeft(&dl, &left))
            return ERR_TIMEOUT;
        EventWaitResult res = WaitOnEvent(r->event, left, Mask(r->data_bit), false);
        if (res.status != ZUZU_OK && res.status != ERR_TIMEOUT)
            return res.status;
    }
}

uint32_t PipeAvail(const PipeEnd *r) { return (r && r->shm) ? RingAvail(r) : 0; }

uint32_t PipeFree(const PipeEnd *w) { return (w && w->shm) ? RingFree(w) : 0; }

uint32_t PipeDropped(const PipeEnd *e) { return (e && e->shm) ? (uint32_t)e->shm->dropped : 0; }

bool PipePeerClosed(const PipeEnd *e)
{
    if (!e || !e->shm)
        return true;
    uint32_t gone = (e->role == PIPE_WRITER) ? PIPE_F_READER_CLOSED : PIPE_F_WRITER_CLOSED;
    return (LoadFlags(e) & gone) != 0;
}

void PipeMarkClosed(PipeEnd *e, PipeRole which)
{
    if (!e || !e->shm || (which != PIPE_WRITER && which != PIPE_READER))
        return;

    uint32_t flag = (which == PIPE_WRITER) ? PIPE_F_WRITER_CLOSED : PIPE_F_READER_CLOSED;
    __atomic_fetch_or(&e->shm->flags, flag, __ATOMIC_RELEASE);

    Kick(e, e->data_bit);
    Kick(e, e->space_bit);
}

void ClosePipe(PipeEnd *e)
{
    if (!e || !e->shm)
        return;

    PipeMarkClosed(e, e->role);
    MemUnmap(e->shm);
    HandleClose(e->mem);
    HandleClose(e->event);
    ForgetEnd(e);
}

#ifndef TTY_PROTOCOL_H
#define TTY_PROTOCOL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <types.h>
#include <util/msg.h>
#include <util/shm_ring.h>
#include <zuzu/err.h>
#include <zuzu/zuzu.h>

#define TTY_NAME_MAX 16
#define TTY_RING_DATA_SIZE 1024u

typedef struct {
    volatile uint32_t eof_seq;
    volatile uint32_t eof_ack;
    volatile uint32_t eof_pos;
    volatile uint32_t intr_seq;
} TtyCtl;

typedef struct {
    ShmRingHdr up_hdr;
    uint8_t up_data[TTY_RING_DATA_SIZE];
    ShmRingHdr down_hdr;
    uint8_t down_data[TTY_RING_DATA_SIZE];
    TtyCtl ctl;
} TtyShm;

_Static_assert(sizeof(TtyShm) <= 4096, "TtyShm must fit in the one shared page");

#define TTY_PROVIDE                                                                                \
    1 /* grant: CreateMem(1) page.  {cmd, alias}; reply TtyConnectReply, grants ttysvc's doorbell  \
       */
#define TTY_ATTACH 2  /* grant: CreateMem(1) page.  same payload/reply as TTY_PROVIDE */
#define TTY_NOTIFY 3  /* grant: the peer's own doorbell. {cmd, index, bit} */
#define TTY_SETMODE 4 /* {cmd, index, flags}, consumers only */
#define TTY_CLOSE 5   /* {cmd, index} */
#define TTY_WATCH                                                                                  \
    6 /* grant: a port the caller owns (PERM_WAIT|PERM_TXFR). {cmd, index}; ttysvc closes the      \
         session when that port dies */

#define TTY_FOCUS 7 /* {cmd, index}, consumers only: take the foreground for input */

#define TTY_MODE_RAW 0u /* default: bytes pass through untouched */
#define TTY_MODE_COOKED                                                                            \
    (1u << 0) /* line editing (^H DEL ^U ^W), whole lines, ^D = EOF, ^C = flag; '\n' -> "\r\n" on  \
                 output */
#define TTY_MODE_ECHO (1u << 1) /* ttysvc echoes typed characters back */
#define TTY_MODE_ALL (TTY_MODE_COOKED | TTY_MODE_ECHO)

typedef struct {
    uint32_t cmd;
    char alias[TTY_NAME_MAX];
} TtyConnectRequest;
typedef struct {
    uint32_t cmd;
    uint32_t index;
    uint32_t bit;
} TtyNotifyRequest;
typedef struct {
    uint32_t cmd;
    uint32_t index;
    uint32_t flags;
} TtySetModeRequest;
typedef struct {
    uint32_t cmd;
    uint32_t index;
} TtyCloseRequest;
typedef TtyCloseRequest TtyWatchRequest;
typedef TtyCloseRequest TtyFocusRequest;
typedef struct {
    Err status;
    uint32_t bit;
    uint32_t index;
} TtyConnectReply;

/* `index` identifies the caller's session in ttysvc: (generation << 8) | slot,
 * so an index from a closed session never matches a reused slot. TODO: it is guessable,
 * so any peer can close or re-mode another's session; bind sessions to
 * unforgeable per-session handles once peers get per-peer authority. */

/*
 * Consumer-side EOF/interrupt checks. EOF is ordered against data: it only
 * reports once everything typed before the ^D has been read from `down`.
 * Call TtyEofPending in the read loop after draining; it consumes the EOF.
 */
static inline bool TtyEofPending(TtyShm *s)
{
    if (s->ctl.eof_seq == s->ctl.eof_ack)
        return false;
    if (s->down_hdr.tail != s->ctl.eof_pos)
        return false;
    s->ctl.eof_ack = s->ctl.eof_seq;
    return true;
}

static inline bool TtyInterrupted(const TtyShm *s, uint32_t *seen)
{
    if (s->ctl.intr_seq == *seen)
        return false;
    *seen = s->ctl.intr_seq;
    return true;
}

/* ---- client helpers (providers and consumers) ---- */

typedef struct {
    TtyShm *shm;
    Handle mem;
    Handle doorbell; /* send-only dup of ttysvc's event */
    uint32_t bit;    /* bit to ring on `doorbell` */
    uint32_t index;
    Handle live; /* port we own; ttysvc watches it to notice our death */
} TtyConn;

/* One request/reply round trip. On success *granted is the handle the reply
 * granted (or -1) and the reply payload is still in MessageBuf(). */
static inline Err TtyCall(Handle tty_port, const void *req, uint32_t len, Handle grant,
                          Handle *granted)
{
    MsgWrite(req, len);
    SvcResult r = Call(tty_port, len, grant);
    if (granted)
        *granted = (Handle)r.r3;
    if (r.r0 != ZUZU_OK)
        return (Err)r.r0;
    Err status;
    if ((uint32_t)r.r1 < sizeof(status))
        return ERR_MALFORMED;
    memcpy(&status, GetMessageBox(), sizeof(status));
    return status;
}

/*
 * cmd is TTY_PROVIDE or TTY_ATTACH. `my_event` is the event the caller waits
 * on; ttysvc rings `my_bit` on it. Returns ERR_NOENT if no endpoint exists
 * yet; consumers that start before their provider should retry.
 */
static inline Err TtyClientConnect(Handle tty_port, uint32_t cmd, const char *alias,
                                   Handle my_event, uint32_t my_bit, TtyConn *c)
{
    memset(c, 0, sizeof(*c));
    c->live = -1;
    c->mem = CreateMem(1, 0);
    if (c->mem < 0)
        return (Err)c->mem;
    void *va = MemMap(c->mem, 0, PROT_RW);
    if (PtrIsErr(va)) {
        HandleClose(c->mem);
        return (Err)va;
    }
    c->shm = (TtyShm *)va;

    /* Grants copy perms verbatim and need PERM_TXFR: send a dup. */
    SvcResult page = HandleDuplicate(c->mem, PERM_MAP | PERM_TXFR, MARKER_NONE);
    if (page.r0 != ZUZU_OK) {
        MemUnmap(va);
        HandleClose(c->mem);
        return (Err)page.r0;
    }

    TtyConnectRequest req = {.cmd = cmd};
    if (alias)
        strncpy(req.alias, alias, TTY_NAME_MAX - 1);
    Handle doorbell = -1;
    Err rc = TtyCall(tty_port, &req, sizeof(req), (Handle)page.r1, &doorbell);
    HandleClose((Handle)page.r1);
    if (rc == ZUZU_OK && doorbell < 0)
        rc = ERR_MALFORMED;
    TtyConnectReply rep;
    if (rc == ZUZU_OK)
        memcpy(&rep, GetMessageBox(), sizeof(rep));
    if (rc != ZUZU_OK) {
        if (doorbell >= 0)
            HandleClose(doorbell);
        MemUnmap(va);
        HandleClose(c->mem);
        return rc;
    }
    c->doorbell = doorbell;
    c->bit = rep.bit;
    c->index = rep.index;

    /* Best effort: without it ttysvc only frees the session on TTY_CLOSE. */
    c->live = CreatePort();
    if (c->live >= 0) {
        SvcResult watch = HandleDuplicate(c->live, PERM_WAIT | PERM_TXFR, MARKER_NONE);
        TtyWatchRequest wreq = {.cmd = TTY_WATCH, .index = c->index};
        Err wrc = (Err)watch.r0;
        if (wrc == ZUZU_OK) {
            wrc = TtyCall(tty_port, &wreq, sizeof(wreq), (Handle)watch.r1, NULL);
            HandleClose((Handle)watch.r1);
        }
        if (wrc != ZUZU_OK) {
            HandleClose(c->live);
            c->live = -1;
        }
    }

    SvcResult mine = HandleDuplicate(my_event, PERM_SEND | PERM_TXFR, MARKER_NONE);
    if (mine.r0 != ZUZU_OK)
        return (Err)mine.r0;
    TtyNotifyRequest notify = {.cmd = TTY_NOTIFY, .index = c->index, .bit = my_bit};
    rc = TtyCall(tty_port, &notify, sizeof(notify), (Handle)mine.r1, NULL);
    HandleClose((Handle)mine.r1);
    return rc;
}

static inline Err TtyClientSetMode(Handle tty_port, const TtyConn *c, uint32_t flags)
{
    TtySetModeRequest req = {.cmd = TTY_SETMODE, .index = c->index, .flags = flags};
    return TtyCall(tty_port, &req, sizeof(req), -1, NULL);
}

static inline Err TtyClientFocus(Handle tty_port, const TtyConn *c)
{
    TtyFocusRequest req = {.cmd = TTY_FOCUS, .index = c->index};
    return TtyCall(tty_port, &req, sizeof(req), -1, NULL);
}

static inline Err TtyClientClose(Handle tty_port, TtyConn *c)
{
    TtyCloseRequest req = {.cmd = TTY_CLOSE, .index = c->index};
    Err rc = TtyCall(tty_port, &req, sizeof(req), -1, NULL);
    HandleClose(c->doorbell);
    MemUnmap(c->shm);
    HandleClose(c->mem);
    if (c->live >= 0)
        HandleClose(c->live);
    c->live = -1;
    c->shm = NULL;
    return rc;
}

#ifdef __cplusplus
}
#endif

#endif

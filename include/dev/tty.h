/**
 * @file
 * @brief ttysvc protocol and client helpers.
 *
 * ttysvc joins providers (UART, keyboard and console drivers) to consumers (programs)
 * through named endpoints. Each session, either kind, shares one page with
 * ttysvc that holds two byte rings: @c up carries bytes from the session's
 * owner to ttysvc, @c down carries bytes from ttysvc to the owner. For a
 * driver, up is what was received and down is what to transmit; for a
 * program, up is its output and down is its input.
 *
 * An endpoint may have several providers, each with a direction. An input
 * provider (a keyboard) only fills its up ring; an output provider (a
 * display) only drains its down ring; a UART does both. Input from every
 * input provider reaches the foreground program, and a program's output goes
 * to every output provider.
 *
 * A session's @c index is (generation << 8) | slot, so an index from a closed
 * session never matches a reused slot. It is guessable, so any peer can close
 * or re-mode another's session.
 */

#ifndef TTY_PROTOCOL_H
#define TTY_PROTOCOL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <types.h>
#include <zuzu/msg.h>
#include <zuzu/shm_ring.h>
#include <zuzu/err.h>
#include <zuzu/zuzu.h>

/** @brief Longest endpoint alias, including the NUL. */
#define TTY_NAME_MAX 16
/** @brief Bytes in each ring. */
#define TTY_RING_DATA_SIZE 1024u

/** @brief Out-of-band signals from ttysvc to a consumer. */
typedef struct {
    volatile uint32_t eof_seq;  /**< Bumped by ttysvc for each ^D in cooked mode. */
    volatile uint32_t eof_ack;  /**< Last eof_seq the consumer has handled. */
    volatile uint32_t eof_pos;  /**< down ring position the EOF applies at. */
    volatile uint32_t intr_seq; /**< Bumped by ttysvc for each ^C while TTY_ISIG is set. */
    volatile uint32_t winch_seq; /**< Bumped by ttysvc when the endpoint's window size changes. */
} TtyCtl;

/** @brief The page a session shares with ttysvc. */
typedef struct {
    ShmRingHdr up_hdr;                     /**< Owner to ttysvc ring. */
    uint8_t up_data[TTY_RING_DATA_SIZE];   /**< Its data. */
    ShmRingHdr down_hdr;                   /**< ttysvc to owner ring. */
    uint8_t down_data[TTY_RING_DATA_SIZE]; /**< Its data. */
    TtyCtl ctl;                            /**< EOF and interrupt signals. */
} TtyShm;

_Static_assert(sizeof(TtyShm) <= 4096, "TtyShm must fit in the one shared page");

/**
 * @brief Provide an endpoint (drivers). TtyConnectRequest; grants the session page. Replies
 * TtyConnectReply and grants ttysvc's doorbell.
 */
#define TTY_PROVIDE 1
/** @brief Attach to an endpoint (programs). Same request and reply as TTY_PROVIDE. */
#define TTY_ATTACH 2
/** @brief Provider direction: supplies input, ttysvc reads its up ring. */
#define TTY_DIR_IN (1u << 0)
/** @brief Provider direction: consumes output, ttysvc fills its down ring. */
#define TTY_DIR_OUT (1u << 1)
/** @brief Provider direction: both, as a UART. The default. */
#define TTY_DIR_BOTH (TTY_DIR_IN | TTY_DIR_OUT)
/** @brief Give ttysvc the caller's event. TtyNotifyRequest; grants the event. */
#define TTY_NOTIFY 3
/** @brief Set a consumer's mode. TtySetModeRequest. */
#define TTY_SETMODE 4
/** @brief End a session. TtyCloseRequest. */
#define TTY_CLOSE 5
/**
 * @brief Tie a session to the caller's life. TtyWatchRequest; grants a port the caller owns
 * (PERM_WAIT | PERM_TXFR). ttysvc closes the session when that port dies.
 */
#define TTY_WATCH 6
/** @brief Take the endpoint's input (consumers). TtyFocusRequest. */
#define TTY_FOCUS 7
/** @brief Read a consumer's mode. TtyGetModeRequest; replies TtyModeReply. */
#define TTY_GETMODE 8
/** @brief Set the endpoint's window size (providers). TtySetWinszRequest. */
#define TTY_SETWINSZ 9
/** @brief Read the endpoint's window size. TtyGetWinszRequest; replies TtyWinszReply. */
#define TTY_GETWINSZ 10

/** @brief Mode: bytes pass through untouched. The default. */
#define TTY_MODE_RAW 0u
/** @brief Input is collected a line at a time, with line editing (^H, DEL, ^U, ^W) and ^D as EOF. */
#define TTY_ICANON (1u << 0)
/** @brief ttysvc echoes typed characters. */
#define TTY_ECHO (1u << 1)
/** @brief ^C raises an interrupt (intr_seq) instead of arriving as a byte. */
#define TTY_ISIG (1u << 2)
/** @brief A typed carriage return becomes a newline. */
#define TTY_ICRNL (1u << 3)
/** @brief Output processing is on; required for TTY_ONLCR. */
#define TTY_OPOST (1u << 4)
/** @brief With TTY_OPOST, a newline in output (and echo) becomes a carriage return and a newline. */
#define TTY_ONLCR (1u << 5)
/** @brief The usual line mode: ICANON, ISIG, ICRNL, OPOST and ONLCR. Echo is separate. */
#define TTY_MODE_COOKED (TTY_ICANON | TTY_ISIG | TTY_ICRNL | TTY_OPOST | TTY_ONLCR)
/** @brief Mode: ttysvc echoes typed characters. */
#define TTY_MODE_ECHO TTY_ECHO
/** @brief Every mode flag. */
#define TTY_MODE_ALL (TTY_ICANON | TTY_ECHO | TTY_ISIG | TTY_ICRNL | TTY_OPOST | TTY_ONLCR)

/** @brief TTY_PROVIDE and TTY_ATTACH request. */
typedef struct {
    uint32_t cmd;             /**< TTY_PROVIDE or TTY_ATTACH. */
    char alias[TTY_NAME_MAX]; /**< Endpoint name; empty attaches to the default endpoint. */
    uint32_t dir;             /**< TTY_PROVIDE: TTY_DIR_* flags, 0 for TTY_DIR_BOTH. Ignored by TTY_ATTACH. */
} TtyConnectRequest;

/** @brief TTY_NOTIFY request. */
typedef struct {
    uint32_t cmd;   /**< TTY_NOTIFY. */
    uint32_t index; /**< Session index. */
    uint32_t bit;   /**< Bit ttysvc rings on the caller's event. */
} TtyNotifyRequest;

/** @brief TTY_SETMODE request. */
typedef struct {
    uint32_t cmd;   /**< TTY_SETMODE. */
    uint32_t index; /**< Session index. */
    uint32_t flags; /**< TTY_MODE_* flags. */
} TtySetModeRequest;

/** @brief TTY_GETMODE request. */
typedef struct {
    uint32_t cmd;   /**< TTY_GETMODE. */
    uint32_t index; /**< Session index. */
} TtyGetModeRequest;

/** @brief TTY_GETMODE reply. */
typedef struct {
    Err status;     /**< ZUZU_OK, or an error code. */
    uint32_t flags; /**< The session's TTY_* mode flags. */
} TtyModeReply;

/** @brief TTY_SETWINSZ request. */
typedef struct {
    uint32_t cmd;   /**< TTY_SETWINSZ. */
    uint32_t index; /**< Provider session index. */
    uint32_t cols;  /**< Character columns. */
    uint32_t rows;  /**< Character rows. */
} TtySetWinszRequest;

/** @brief TTY_GETWINSZ request. */
typedef struct {
    uint32_t cmd;   /**< TTY_GETWINSZ. */
    uint32_t index; /**< Session index. */
} TtyGetWinszRequest;

/** @brief TTY_GETWINSZ reply. */
typedef struct {
    Err status;    /**< ZUZU_OK, ERR_NOENT if no provider has set a size. */
    uint32_t cols; /**< Character columns. */
    uint32_t rows; /**< Character rows. */
} TtyWinszReply;

/** @brief TTY_CLOSE request. */
typedef struct {
    uint32_t cmd;   /**< The command. */
    uint32_t index; /**< Session index. */
} TtyCloseRequest;

/** @brief TTY_WATCH request; same layout as TtyCloseRequest. */
typedef TtyCloseRequest TtyWatchRequest;
/** @brief TTY_FOCUS request; same layout as TtyCloseRequest. */
typedef TtyCloseRequest TtyFocusRequest;

/** @brief TTY_PROVIDE and TTY_ATTACH reply. */
typedef struct {
    Err status;     /**< ZUZU_OK, or an error code. */
    uint32_t bit;   /**< Bit to ring on ttysvc's doorbell. */
    uint32_t index; /**< Session index for later requests. */
} TtyConnectReply;

/**
 * @brief Consumer: tells whether an EOF is due, and consumes it.
 *
 * EOF is ordered against data: it only reports once everything typed before
 * the ^D has been read from @c down. Call it in the read loop after draining.
 *
 * @param[in,out] s Session page.
 * @return true once, for each ^D.
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

/**
 * @brief Consumer: tells whether ^C was pressed since the last check.
 *
 * @param[in]     s    Session page.
 * @param[in,out] seen Last intr_seq seen; updated.
 * @return true if there has been an interrupt since @p seen.
 */
static inline bool TtyInterrupted(const TtyShm *s, uint32_t *seen)
{
    if (s->ctl.intr_seq == *seen)
        return false;
    *seen = s->ctl.intr_seq;
    return true;
}

/**
 * @brief Consumer: tells whether the window size changed since the last check.
 *
 * @param[in]     s    Session page.
 * @param[in,out] seen Last winch_seq seen; updated.
 * @return true if the size changed since @p seen.
 */
static inline bool TtyWinchPending(const TtyShm *s, uint32_t *seen)
{
    if (s->ctl.winch_seq == *seen)
        return false;
    *seen = s->ctl.winch_seq;
    return true;
}

/** @brief A session with ttysvc, for providers and consumers alike. */
typedef struct {
    TtyShm *shm;     /**< Mapped session page. */
    Handle mem;      /**< The session page's memory object. */
    Handle doorbell; /**< ttysvc's event, send-only. */
    uint32_t bit;    /**< Bit to ring on @c doorbell. */
    uint32_t index;  /**< Session index. */
    Handle live;     /**< Port this client owns; ttysvc watches it to notice the client's death. */
} TtyConn;

/**
 * @brief One request and reply with ttysvc.
 *
 * @param[in]  tty_port Handle to ttysvc.
 * @param[in]  req      Request.
 * @param[in]  len      Request length.
 * @param[in]  grant    Handle to grant, or -1.
 * @param[out] granted  Handle the reply granted, or -1; may be NULL.
 * @return ttysvc's status; on ZUZU_OK the reply is still in the message buffer.
 */
static inline Err TtyCall(Handle tty_port, const void *req, uint32_t len, Handle grant,
                          Handle *granted)
{
    return MsgCallStatus(tty_port, req, len, grant, granted);
}

/**
 * @brief Opens a session: creates the page, connects, asks ttysvc to watch the caller, and
 * hands over the caller's event.
 *
 * @param[in]  tty_port Handle to ttysvc.
 * @param[in]  cmd      TTY_PROVIDE or TTY_ATTACH.
 * @param[in]  alias    Endpoint name, or NULL for the default.
 * @param[in]  dir      TTY_PROVIDE only: TTY_DIR_* flags, 0 for TTY_DIR_BOTH.
 * @param[in]  my_event Event the caller waits on.
 * @param[in]  my_bit   Bit ttysvc rings on it.
 * @param[out] c        The session.
 * @return ZUZU_OK, or an error code.
 * @retval ERR_NOENT     No such endpoint yet; a consumer that starts before its provider retries.
 * @retval ERR_MALFORMED ttysvc did not grant a doorbell.
 * @retval ERR_BADARG    @p dir has unknown flags.
 * @retval ERR_BUSY      The endpoint already has its most providers.
 */
static inline Err TtyClientConnectDir(Handle tty_port, uint32_t cmd, const char *alias,
                                      uint32_t dir, Handle my_event, uint32_t my_bit, TtyConn *c)
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

    Handle page;
    Err prc = HandleDup(c->mem, PERM_MAP | PERM_WRITE | PERM_TXFR, MARKER_NONE, &page);
    if (prc != ZUZU_OK) {
        MemUnmap(va);
        HandleClose(c->mem);
        return prc;
    }

    TtyConnectRequest req = {.cmd = cmd, .dir = dir};
    if (alias)
        strncpy(req.alias, alias, TTY_NAME_MAX - 1);
    Handle doorbell = -1;
    Err rc = TtyCall(tty_port, &req, sizeof(req), page, &doorbell);
    HandleClose(page);
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

    c->live = CreatePort();
    if (c->live >= 0) {
        Handle watch;
        TtyWatchRequest wreq = {.cmd = TTY_WATCH, .index = c->index};
        Err wrc = HandleDup(c->live, PERM_WAIT | PERM_TXFR, MARKER_NONE, &watch);
        if (wrc == ZUZU_OK) {
            wrc = TtyCall(tty_port, &wreq, sizeof(wreq), watch, NULL);
            HandleClose(watch);
        }
        if (wrc != ZUZU_OK) {
            HandleClose(c->live);
            c->live = -1;
        }
    }

    Handle mine;
    rc = HandleDup(my_event, PERM_SEND | PERM_TXFR, MARKER_NONE, &mine);
    if (rc != ZUZU_OK)
        return rc;
    TtyNotifyRequest notify = {.cmd = TTY_NOTIFY, .index = c->index, .bit = my_bit};
    rc = TtyCall(tty_port, &notify, sizeof(notify), mine, NULL);
    HandleClose(mine);
    return rc;
}

/**
 * @brief TtyClientConnectDir() with TTY_DIR_BOTH: the usual provider or consumer session.
 *
 * @param[in]  tty_port Handle to ttysvc.
 * @param[in]  cmd      TTY_PROVIDE or TTY_ATTACH.
 * @param[in]  alias    Endpoint name, or NULL for the default.
 * @param[in]  my_event Event the caller waits on.
 * @param[in]  my_bit   Bit ttysvc rings on it.
 * @param[out] c        The session.
 * @return As TtyClientConnectDir().
 */
static inline Err TtyClientConnect(Handle tty_port, uint32_t cmd, const char *alias,
                                   Handle my_event, uint32_t my_bit, TtyConn *c)
{
    return TtyClientConnectDir(tty_port, cmd, alias, 0, my_event, my_bit, c);
}

/**
 * @brief Sets a consumer's mode.
 *
 * @param[in] tty_port Handle to ttysvc.
 * @param[in] c        The session.
 * @param[in] flags    TTY_MODE_* flags.
 * @return ttysvc's status.
 */
static inline Err TtyClientSetMode(Handle tty_port, const TtyConn *c, uint32_t flags)
{
    TtySetModeRequest req = {.cmd = TTY_SETMODE, .index = c->index, .flags = flags};
    return TtyCall(tty_port, &req, sizeof(req), -1, NULL);
}

/**
 * @brief Reads a consumer's mode.
 *
 * @param[in]  tty_port Handle to ttysvc.
 * @param[in]  c        The session.
 * @param[out] flags    The TTY_* mode flags.
 * @return ttysvc's status.
 */
static inline Err TtyClientGetMode(Handle tty_port, const TtyConn *c, uint32_t *flags)
{
    TtyGetModeRequest req = {.cmd = TTY_GETMODE, .index = c->index};
    Err rc = TtyCall(tty_port, &req, sizeof(req), -1, NULL);
    if (rc != ZUZU_OK)
        return rc;
    TtyModeReply rep;
    memcpy(&rep, GetMessageBox(), sizeof(rep));
    *flags = rep.flags;
    return rep.status;
}

/**
 * @brief Sets the endpoint's window size (providers).
 *
 * @param[in] tty_port Handle to ttysvc.
 * @param[in] c        The provider session.
 * @param[in] cols     Character columns.
 * @param[in] rows     Character rows.
 * @return ttysvc's status.
 */
static inline Err TtyClientSetWinsz(Handle tty_port, const TtyConn *c, uint32_t cols, uint32_t rows)
{
    TtySetWinszRequest req = {.cmd = TTY_SETWINSZ, .index = c->index, .cols = cols, .rows = rows};
    return TtyCall(tty_port, &req, sizeof(req), -1, NULL);
}

/**
 * @brief Reads the endpoint's window size.
 *
 * @param[in]  tty_port Handle to ttysvc.
 * @param[in]  c        The session.
 * @param[out] cols     Character columns.
 * @param[out] rows     Character rows.
 * @return ttysvc's status; ERR_NOENT if no provider has set a size.
 */
static inline Err TtyClientGetWinsz(Handle tty_port, const TtyConn *c, uint32_t *cols,
                                    uint32_t *rows)
{
    TtyGetWinszRequest req = {.cmd = TTY_GETWINSZ, .index = c->index};
    Err rc = TtyCall(tty_port, &req, sizeof(req), -1, NULL);
    if (rc != ZUZU_OK)
        return rc;
    TtyWinszReply rep;
    memcpy(&rep, GetMessageBox(), sizeof(rep));
    *cols = rep.cols;
    *rows = rep.rows;
    return rep.status;
}

/**
 * @brief Makes a consumer the one that receives the endpoint's input.
 *
 * @param[in] tty_port Handle to ttysvc.
 * @param[in] c        The session.
 * @return ttysvc's status.
 */
static inline Err TtyClientFocus(Handle tty_port, const TtyConn *c)
{
    TtyFocusRequest req = {.cmd = TTY_FOCUS, .index = c->index};
    return TtyCall(tty_port, &req, sizeof(req), -1, NULL);
}

/**
 * @brief Closes a session and releases its page and handles, whatever ttysvc answers.
 *
 * @param[in]     tty_port Handle to ttysvc.
 * @param[in,out] c        The session.
 * @return ttysvc's status.
 */
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

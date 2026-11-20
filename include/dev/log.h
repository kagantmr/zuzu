/**
 * @file log.h
 * @brief Client interface to the log service (logsvc).
 *
 * Each client owns one pipe to logsvc and writes records into it without blocking. Then logsvc
 * stamps every record with the time, a sequence number and the id of the session it came from, and
 * keeps the most recent ones. A reader asks for records after a sequence number through a second
 * pipe in the other direction.
 *
 */

#ifndef DEV_LOG_H
#define DEV_LOG_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <types.h>
#include <util/log.h>
#include <util/pipe.h>
#include <zuzu/err.h>
#include <zuzu/msg.h>
#include <zuzu/service.h>
#include <zuzu/zuzu.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Most sessions logsvc keeps: one doorbell bit and one death bit each. */
#define LOG_MAX_SESSIONS 15
/** @brief Longest message in a record in bytes. */
#define LOG_MSG_MAX 512
/** @brief Bytes in a source tag  */
#define LOG_SOURCE_MAX 8
/** @brief Ring size of a writer's pipe */
#define LOG_PIPE_SIZE 65536u
/** @brief Ring size of a reader's pipe. */
#define LOG_READER_PIPE_SIZE PIPE_SIZE_DEFAULT
/** @brief Bytes of history logsvc keeps. */
#define LOG_HISTORY_SIZE 65536u
/** @brief Bytes of crash history logsvc keeps apart so debug output cannot evict it. */
#define LOG_CRASH_SIZE 8192u
/** @brief Longest message a LOG_CRASH request can carry. */
#define LOG_CRASH_MSG_MAX 200

/** @brief A record as a client writes it. */
typedef struct {
    uint8_t level;               /**< LogLevel, 0 to 5. */
    uint8_t reserved;            /**< Zero. */
    uint16_t length;             /**< Message bytes after the header. */
    char source[LOG_SOURCE_MAX]; /**< The client's own tag: a hint, never trusted. */
} LogWireHdr;
_Static_assert(sizeof(LogWireHdr) == 12, "LogWireHdr is 12 bytes");

/** @brief A record as logsvc stores it and as a reader receives it. */
typedef struct {
    uint64_t timestamp_us;       /**< Microseconds since boot, stamped by logsvc on receipt. */
    uint32_t seq;                /**< Counts up and wraps; compare with serial arithmetic. */
    uint16_t length;             /**< Message bytes after the header. */
    uint8_t level;               /**< LogLevel, 0 to 5. */
    uint8_t client;              /**< logsvc's id for the sending session. Trusted. */
    char source[LOG_SOURCE_MAX]; /**< The client's tag, as it sent it. */
} LogEntryHdr;
_Static_assert(sizeof(LogEntryHdr) == 24, "LogEntryHdr is 24 bytes");

/** @brief Bytes a client record takes in its pipe: header and message, padded to 4. */
#define LOG_WIRE_SIZE(len) (((sizeof(LogWireHdr) + (size_t)(len)) + 3u) & ~(size_t)3u)
/** @brief Bytes a stored or delivered record takes: header and message, padded to 8. */
#define LOG_ENTRY_SIZE(len) (((sizeof(LogEntryHdr) + (size_t)(len)) + 7u) & ~(size_t)7u)

/**
 * @brief Open a session. 
 *
 * The page comes from the client's CreatePipe(). For a writer, logsvc reads the pipe; for a reader,
 * logsvc writes it.
 */
#define LOG_ATTACH 1
/** @brief Give logsvc the pipe's event. LogIndexRequest; grants the event. Session handle. */
#define LOG_EVENT 2
/** @brief End a session. LogIndexRequest. Session handle. */
#define LOG_CLOSE 4
/** @brief Reader: choose what to receive. LogReadRequest. Session handle. */
#define LOG_READ 5
/**
 * @brief Set one source's minimum level. LogLevelRequest.
 *
 * A session handle may set the level of its own source tag only; the admin handle may set any.
 */
#define LOG_SETLEVEL 6
/** @brief Read counters. LogIndexRequest; replies LogStatsReply. Any handle. */
#define LOG_STATS 7
/** @brief Store a record in the crash history. LogCrashRequest. Admin handle only. */
#define LOG_CRASH 8
/**
 * @brief Take the admin handle. LogIndexRequest; grants the admin handle. Public handle only.
 *
 * Succeeds once per logsvc: the first caller gets it, and later calls return ERR_BUSY.
 */
#define LOG_CLAIM 9

/** @brief Session kind: the client writes records, logsvc reads them. */
#define LOG_KIND_WRITER 1
/** @brief Session kind: logsvc writes records, the client reads them. */
#define LOG_KIND_READER 2

/** @brief LOG_READ flag: when caught up, keep the session open and send new records as they come.
 */
#define LOG_READ_FOLLOW (1u << 0)

/** @brief LOG_ATTACH request. */
typedef struct {
    uint32_t cmd;                /**< LOG_ATTACH. */
    uint32_t kind;               /**< LOG_KIND_WRITER or LOG_KIND_READER. */
    char source[LOG_SOURCE_MAX]; /**< Writer: the tag to use when a record has none. */
} LogAttachRequest;

/** @brief LOG_ATTACH reply. */
typedef struct {
    Err status;     /**< ZUZU_OK, or an error code. */
    uint32_t index; /**< Session index for later requests. */
} LogAttachReply;

/** @brief LOG_EVENT, LOG_CLOSE, LOG_STATS and LOG_CLAIM request. The index is ignored: the session
 * is the one the handle belongs to. */
typedef struct {
    uint32_t cmd;   /**< The command. */
    uint32_t index; /**< Session index. */
} LogIndexRequest;

/** @brief LOG_READ request. */
typedef struct {
    uint32_t cmd;                /**< LOG_READ. */
    uint32_t index;              /**< Session index of a reader. */
    uint32_t from_seq;           /**< First sequence number wanted. */
    uint32_t flags;              /**< LOG_READ_*. */
    uint8_t min_level;           /**< Skip records below this LogLevel. */
    uint8_t reserved[3];         /**< Zero. */
    char source[LOG_SOURCE_MAX]; /**< Only records with this tag; all zero for any. */
} LogReadRequest;

/** @brief LOG_SETLEVEL request. */
typedef struct {
    uint32_t cmd;                /**< LOG_SETLEVEL. */
    uint32_t level;              /**< The new minimum LogLevel. */
    char source[LOG_SOURCE_MAX]; /**< The source it applies to. */
} LogLevelRequest;

/** @brief LOG_STATS reply. */
typedef struct {
    Err status;             /**< ZUZU_OK, or an error code. */
    uint32_t sessions;      /**< Open sessions. */
    uint32_t next_seq;      /**< The sequence number the next record gets. */
    uint32_t records;       /**< Records in the history now. */
    uint32_t dropped;       /**< Records clients could not fit, added up over all sessions. */
    uint32_t malformed;     /**< Records logsvc threw away as invalid. */
} LogStatsReply;

/** @brief LOG_CRASH request: a header, then @c length message bytes. */
typedef struct {
    uint32_t cmd;                /**< LOG_CRASH. */
    uint8_t level;               /**< LogLevel, 0 to 5. */
    uint8_t reserved;            /**< Zero. */
    uint16_t length;             /**< Message bytes after the header, at most LOG_CRASH_MSG_MAX. */
    char source[LOG_SOURCE_MAX]; /**< Who died. */
} LogCrashRequest;

/** @brief A client's session. */
typedef struct {
    Handle port;    /**< The session handle. Closed by LogClientClose(). */
    uint32_t index; /**< Session index at logsvc. */
    PipeEnd w;      /**< The writer end of the pipe, in this space. */
} LogConn;

/**
 * @brief Sends one request to logsvc.
 *
 * @param[in]  log_port Handle to logsvc.
 * @param[in]  req      The request.
 * @param[in]  len      Its length.
 * @param[in]  grant    A handle to give logsvc, or -1.
 * @param[out] granted  A handle logsvc gives back, or NULL.
 * @return The status in the reply, or the call's error.
 */
static inline Err LogCall(Handle log_port, const void *req, uint32_t len, Handle grant,
                          Handle *granted)
{
    return MsgCallStatus(log_port, req, len, grant, granted);
}

/**
 * @brief Opens a writer session: makes the pipe and hands logsvc its reading end.
 *
 * @param[in]  log_port The public handle to logsvc. The caller keeps it.
 * @param[in]  source   The caller's tag, or NULL.
 * @param[out] c        The session.
 * @return ZUZU_OK, or an error code.
 * @retval ERR_BUSY logsvc has no free session.
 */
static inline Err LogClientConnect(Handle log_port, const char *source, LogConn *c)
{
    memset(c, 0, sizeof(*c));
    c->port = -1;

    PipeEnd r;
    Err rc = CreatePipe(LOG_PIPE_SIZE, &c->w, &r);
    if (rc != ZUZU_OK)
        return rc;

    Handle mem = -1, ev = -1;
    rc = PipeDupHandles(&r, PERM_SEND | PERM_WAIT | PERM_TXFR, &mem, &ev);
    PipeDetach(&r); /* our copy of the reading end: let go without marking it closed */
    if (rc != ZUZU_OK) {
        ClosePipe(&c->w);
        return rc;
    }

    LogAttachRequest areq = {.cmd = LOG_ATTACH, .kind = LOG_KIND_WRITER};
    if (source)
        strncpy(areq.source, source, LOG_SOURCE_MAX);
    Handle session = -1;
    rc = LogCall(log_port, &areq, sizeof(areq), mem, &session);
    HandleClose(mem);
    if (rc == ZUZU_OK && session < 0)
        rc = ERR_MALFORMED;
    if (rc != ZUZU_OK) {
        HandleClose(ev);
        ClosePipe(&c->w);
        return rc;
    }
    c->port = session;
    LogAttachReply arep;
    memcpy(&arep, GetMessageBox(), sizeof(arep));
    c->index = arep.index;

    LogIndexRequest ereq = {.cmd = LOG_EVENT, .index = c->index};
    rc = LogCall(c->port, &ereq, sizeof(ereq), ev, NULL);
    HandleClose(ev);
    if (rc != ZUZU_OK) {
        LogIndexRequest creq = {.cmd = LOG_CLOSE, .index = c->index};
        LogCall(c->port, &creq, sizeof(creq), -1, NULL);
        HandleClose(c->port);
        c->port = -1;
        ClosePipe(&c->w);
        return rc;
    }
    return ZUZU_OK;
}

/**
 * @brief Writes one record. Never blocks; drops the record if the pipe is full.
 *
 * @param[in] c      The session.
 * @param[in] level  A LogLevel.
 * @param[in] source The tag for this record, or NULL for the session's.
 * @param[in] msg    The message, not NUL-terminated.
 * @param[in] len    Its length; cut to LOG_MSG_MAX.
 * @return ZUZU_OK; ERR_BUFFULL if the record was dropped (counted by the pipe); ERR_DEAD if logsvc
 *         has gone.
 */
static inline Err LogClientWrite(LogConn *c, uint8_t level, const char *source, const char *msg,
                                 uint16_t len)
{
    if (len > LOG_MSG_MAX)
        len = LOG_MSG_MAX;

    /* One all-or-nothing write, so logsvc never sees half a record. */
    uint8_t rec[LOG_WIRE_SIZE(LOG_MSG_MAX)];
    size_t size = LOG_WIRE_SIZE(len);
    memset(rec, 0, size);

    LogWireHdr h = {.level = level, .length = len};
    if (source)
        strncpy(h.source, source, LOG_SOURCE_MAX);
    memcpy(rec, &h, sizeof(h));
    memcpy(rec + sizeof(h), msg, len);
    return PipeTryWriteAll(&c->w, rec, size);
}

/**
 * @brief Closes a writer session and its handle.
 *
 * @param[in] c The session, left unusable.
 */
static inline void LogClientClose(LogConn *c)
{
    LogIndexRequest req = {.cmd = LOG_CLOSE, .index = c->index};
    LogCall(c->port, &req, sizeof(req), -1, NULL);
    HandleClose(c->port);
    c->port = -1;
    ClosePipe(&c->w);
}

/**
 * @brief Takes the admin handle. Works once per logsvc; rootsvc does it at boot.
 *
 * @param[in]  log_port The public handle to logsvc.
 * @param[out] admin    The admin handle.
 * @return ZUZU_OK, or an error code.
 * @retval ERR_BUSY The admin handle was already claimed.
 */
static inline Err LogAdminClaim(Handle log_port, Handle *admin)
{
    LogIndexRequest req = {.cmd = LOG_CLAIM};
    Err rc = LogCall(log_port, &req, sizeof(req), -1, admin);
    if (rc == ZUZU_OK && *admin < 0)
        rc = ERR_MALFORMED;
    return rc;
}

/**
 * @brief Stores a crash record in logsvc's crash history. For rootsvc and similar supervisors.
 *
 * @param[in] log_port The admin handle.
 * @param[in] level    A LogLevel.
 * @param[in] source   Who died, or NULL.
 * @param[in] msg      The message, not NUL-terminated.
 * @param[in] len      Its length; cut to LOG_CRASH_MSG_MAX.
 * @return ZUZU_OK, or an error code.
 */
static inline Err LogClientCrash(Handle log_port, uint8_t level, const char *source,
                                 const char *msg, uint16_t len)
{
    if (len > LOG_CRASH_MSG_MAX)
        len = LOG_CRASH_MSG_MAX;
    uint8_t buf[sizeof(LogCrashRequest) + LOG_CRASH_MSG_MAX];
    LogCrashRequest h = {.cmd = LOG_CRASH, .level = level, .length = len};
    if (source)
        strncpy(h.source, source, LOG_SOURCE_MAX);
    memcpy(buf, &h, sizeof(h));
    memcpy(buf + sizeof(h), msg, len);
    return LogCall(log_port, buf, (uint32_t)(sizeof(h) + len), -1, NULL);
}

/** @brief A reader's session. */
typedef struct {
    Handle port;    /**< The session handle. Closed by LogReaderClose(). */
    uint32_t index; /**< Session index at logsvc. */
    PipeEnd r;      /**< The reading end of the pipe logsvc writes. */
} LogReader;

/**
 * @brief Opens a reader session and asks for records.
 *
 * @param[in]  log_port The public handle to logsvc. The caller keeps it.
 * @param[in]  from_seq First sequence number wanted.
 * @param[in]  min_level Skip records below this LogLevel.
 * @param[in]  source   Only this source tag, or NULL for any.
 * @param[in]  flags    LOG_READ_*.
 * @param[out] rd       The session.
 * @return ZUZU_OK, or an error code.
 * @retval ERR_BUSY logsvc has no free session.
 */
static inline Err LogReaderOpen(Handle log_port, uint32_t from_seq, uint8_t min_level,
                                const char *source, uint32_t flags, LogReader *rd)
{
    memset(rd, 0, sizeof(*rd));
    rd->port = -1;

    PipeEnd w;
    Err rc = CreatePipe(LOG_READER_PIPE_SIZE, &w, &rd->r);
    if (rc != ZUZU_OK)
        return rc;

    Handle mem = -1, ev = -1;
    rc = PipeDupHandles(&w, PERM_SEND | PERM_WAIT | PERM_TXFR, &mem, &ev);
    PipeDetach(&w); /* logsvc owns the writing end from here: do not mark it closed */
    if (rc != ZUZU_OK) {
        ClosePipe(&rd->r);
        return rc;
    }

    LogAttachRequest areq = {.cmd = LOG_ATTACH, .kind = LOG_KIND_READER};
    Handle session = -1;
    rc = LogCall(log_port, &areq, sizeof(areq), mem, &session);
    HandleClose(mem);
    if (rc == ZUZU_OK && session < 0)
        rc = ERR_MALFORMED;
    if (rc != ZUZU_OK) {
        HandleClose(ev);
        ClosePipe(&rd->r);
        return rc;
    }
    rd->port = session;
    LogAttachReply arep;
    memcpy(&arep, GetMessageBox(), sizeof(arep));
    rd->index = arep.index;

    LogIndexRequest ereq = {.cmd = LOG_EVENT, .index = rd->index};
    rc = LogCall(rd->port, &ereq, sizeof(ereq), ev, NULL);
    HandleClose(ev);
    if (rc == ZUZU_OK) {
        LogReadRequest rreq = {
            .cmd = LOG_READ, .index = rd->index, .from_seq = from_seq, .flags = flags,
            .min_level = min_level};
        if (source)
            strncpy(rreq.source, source, LOG_SOURCE_MAX);
        rc = LogCall(rd->port, &rreq, sizeof(rreq), -1, NULL);
    }
    if (rc != ZUZU_OK) {
        LogIndexRequest creq = {.cmd = LOG_CLOSE, .index = rd->index};
        LogCall(rd->port, &creq, sizeof(creq), -1, NULL);
        HandleClose(rd->port);
        rd->port = -1;
        ClosePipe(&rd->r);
    }
    return rc;
}

/**
 * @brief Reads exactly @p n bytes from a reader's pipe.
 *
 * @param[in]  r       The pipe.
 * @param[out] dst     Destination.
 * @param[in]  n       Bytes wanted.
 * @param[in]  timeout As for PipeRead().
 * @return @p n; 0 at EOF before any byte; a negative Err on timeout or a cut record.
 */
static inline int LogReadFull(PipeEnd *r, void *dst, size_t n, Duration timeout)
{
    uint8_t *p = dst;
    size_t got = 0;
    while (got < n) {
        int k = PipeRead(r, p + got, n - got, timeout);
        if (k < 0)
            return k;
        if (k == 0)
            return got == 0 ? 0 : ERR_MALFORMED;
        got += (size_t)k;
    }
    return (int)n;
}

/**
 * @brief Reads the next record.
 *
 * @param[in]  rd      The session.
 * @param[out] h       The record's header.
 * @param[out] msg     Room for LOG_MSG_MAX bytes; not NUL-terminated.
 * @param[in]  timeout TIMEOUT_POLL, TIMEOUT_INFINITE, or milliseconds.
 * @return 1 for a record; 0 at the end of the history; ERR_TIMEOUT, or another negative Err.
 */
static inline int LogReaderNext(LogReader *rd, LogEntryHdr *h, char *msg, Duration timeout)
{
    int rc = LogReadFull(&rd->r, h, sizeof(*h), timeout);
    if (rc <= 0)
        return rc;
    if (h->length > LOG_MSG_MAX)
        return ERR_MALFORMED;
    size_t body = LOG_ENTRY_SIZE(h->length) - sizeof(*h);
    uint8_t buf[LOG_ENTRY_SIZE(LOG_MSG_MAX) - sizeof(LogEntryHdr)];
    rc = LogReadFull(&rd->r, buf, body, TIMEOUT_INFINITE);
    if (rc <= 0)
        return rc == 0 ? ERR_MALFORMED : rc;
    memcpy(msg, buf, h->length);
    return 1;
}

/**
 * @brief Closes a reader session and its handle.
 *
 * @param[in] rd The session, left unusable.
 */
static inline void LogReaderClose(LogReader *rd)
{
    LogIndexRequest req = {.cmd = LOG_CLOSE, .index = rd->index};
    LogCall(rd->port, &req, sizeof(req), -1, NULL);
    HandleClose(rd->port);
    rd->port = -1;
    ClosePipe(&rd->r);
}

#ifdef __cplusplus
}
#endif

#endif /* DEV_LOG_H */

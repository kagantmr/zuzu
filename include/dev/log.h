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
/** @brief Ring size of a session's pipe: one data page. */
#define LOG_PIPE_SIZE PIPE_SIZE_MIN

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
 * @brief Open a session. LogAttachRequest; grants the pipe's page. Replies LogAttachReply.
 *
 * The page comes from the client's CreatePipe(). For a writer, logsvc reads the pipe; for a reader,
 * logsvc writes it.
 */
#define LOG_ATTACH 1
/** @brief Give logsvc the pipe's event. LogIndexRequest; grants the event. */
#define LOG_EVENT 2
/**
 * @brief Tie the session to the caller's life. LogIndexRequest; grants a PERM_WAIT-only handle to
 * the caller's own space. logsvc frees the session when that space dies.
 */
#define LOG_WATCH 3
/** @brief End a session. LogIndexRequest. */
#define LOG_CLOSE 4
/** @brief Reader: choose what to receive. LogReadRequest. */
#define LOG_READ 5
/** @brief Set the console mirror level, or one source's level. LogLevelRequest. */
#define LOG_SETLEVEL 6
/** @brief Read counters. LogIndexRequest; replies LogStatsReply. */
#define LOG_STATS 7

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

/** @brief LOG_EVENT, LOG_WATCH, LOG_CLOSE and LOG_STATS request. */
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
    char source[LOG_SOURCE_MAX]; /**< The source it applies to; all zero for the console mirror. */
} LogLevelRequest;

/** @brief LOG_STATS reply. */
typedef struct {
    Err status;             /**< ZUZU_OK, or an error code. */
    uint32_t sessions;      /**< Open sessions. */
    uint32_t next_seq;      /**< The sequence number the next record gets. */
    uint32_t records;       /**< Records in the history now. */
    uint32_t dropped;       /**< Records clients could not fit, added up over all sessions. */
    uint32_t malformed;     /**< Records logsvc threw away as invalid. */
    uint32_t console_level; /**< Current console mirror level. */
} LogStatsReply;

/** @brief A client's session. */
typedef struct {
    Handle port;    /**< logsvc's port. */
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
 * @brief Opens a writer session: makes the pipe, hands logsvc its reading end, and asks logsvc to
 * watch the caller.
 *
 * @param[in]  log_port Handle to logsvc.
 * @param[in]  source   The caller's tag, or NULL.
 * @param[out] c        The session.
 * @return ZUZU_OK, or an error code.
 * @retval ERR_BUSY logsvc has no free session.
 */
static inline Err LogClientConnect(Handle log_port, const char *source, LogConn *c)
{
    memset(c, 0, sizeof(*c));
    c->port = log_port;

    PipeEnd r;
    Err rc = CreatePipe(LOG_PIPE_SIZE, &c->w, &r);
    if (rc != ZUZU_OK)
        return rc;

    Handle mem = -1, ev = -1;
    rc = PipeDupHandles(&r, PERM_SEND | PERM_WAIT, &mem, &ev);
    PipeDetach(&r); /* our copy of the reading end: let go without marking it closed */
    if (rc != ZUZU_OK) {
        ClosePipe(&c->w);
        return rc;
    }

    LogAttachRequest areq = {.cmd = LOG_ATTACH, .kind = LOG_KIND_WRITER};
    if (source)
        strncpy(areq.source, source, LOG_SOURCE_MAX);
    rc = LogCall(log_port, &areq, sizeof(areq), mem, NULL);
    HandleClose(mem);
    if (rc != ZUZU_OK) {
        HandleClose(ev);
        ClosePipe(&c->w);
        return rc;
    }
    LogAttachReply arep;
    memcpy(&arep, GetMessageBox(), sizeof(arep));
    c->index = arep.index;

    LogIndexRequest ereq = {.cmd = LOG_EVENT, .index = c->index};
    rc = LogCall(log_port, &ereq, sizeof(ereq), ev, NULL);
    HandleClose(ev);
    if (rc != ZUZU_OK) {
        LogIndexRequest creq = {.cmd = LOG_CLOSE, .index = c->index};
        LogCall(log_port, &creq, sizeof(creq), -1, NULL);
        ClosePipe(&c->w);
        return rc;
    }

#ifdef SELF_SPACE
    Handle watch = -1;
    if (HandleDup(SELF_SPACE, PERM_WAIT, MARKER_NONE, &watch) == ZUZU_OK) {
        LogIndexRequest wreq = {.cmd = LOG_WATCH, .index = c->index};
        LogCall(log_port, &wreq, sizeof(wreq), watch, NULL);
        HandleClose(watch);
    }
#endif
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
 * @brief Closes a writer session.
 *
 * @param[in] c The session, left unusable.
 */
static inline void LogClientClose(LogConn *c)
{
    LogIndexRequest req = {.cmd = LOG_CLOSE, .index = c->index};
    LogCall(c->port, &req, sizeof(req), -1, NULL);
    ClosePipe(&c->w);
}

#ifdef __cplusplus
}
#endif

#endif /* DEV_LOG_H */

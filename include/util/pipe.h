/**
 * @file pipe.h
 *
 * @brief Pipes are a one way stream between spaces.
 */

#ifndef UTIL_PIPE_H
#define UTIL_PIPE_H

#include <stdint.h>
#include <types.h>
#include <zuzu/err.h>
#include <zuzu/shm_ring.h>
#include <zuzu/zuzu.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Page size the pipe layout is built on. */
#define PIPE_PAGE_SIZE 4096u
/** @brief The header occupies page 0; the ring's data starts on page 1. */
#define PIPE_HDR_SIZE PIPE_PAGE_SIZE
/** @brief Smallest ring: one data page. Enough for the log client rings. */
#define PIPE_SIZE_MIN 4096u
/** @brief Default ring for shell pipelines: four data pages, five pages in all. */
#define PIPE_SIZE_DEFAULT 16384u
/** @brief Largest ring. */
#define PIPE_SIZE_MAX 262144u

/** @brief The writer is gone. A reader sees EOF once the ring drains. */
#define PIPE_F_WRITER_CLOSED (1u << 0)
/** @brief The reader is gone. A writer's next write fails. */
#define PIPE_F_READER_CLOSED (1u << 1)

/** @brief Which end of a pipe a process holds. */
typedef enum {
    PIPE_WRITER = 1, /**< Produces bytes. Waits on the space bit, signals the data bit. */
    PIPE_READER = 2  /**< Consumes bytes. Waits on the data bit, signals the space bit. */
} PipeRole;

/** @brief The header at the start of the shared page. */
typedef struct {
    ShmRingHdr ring;           /**< The byte ring; size is a power of two. */
    volatile uint32_t flags;   /**< PIPE_F_*. */
    volatile size_t dropped; /**< PipeTryWriteAll() calls that found no room. */
} PipeShm;
_Static_assert(sizeof(PipeShm) <= PIPE_HDR_SIZE, "PipeShm must fit its reserved header");

/** @brief One end of a pipe, as a sppace holds it. */
typedef struct {
    PipeShm *shm;       /**< The mapped page. */
    uint8_t *data;      /**< The ring's data area, PIPE_HDR_SIZE bytes into the page. */
    size_t size;      /**< Ring size in bytes. */
    Handle mem;         /**< The page's memory object. */
    Handle event;       /**< The event, shared with the peer or its own. */
    size_t data_bit;  /**< Event bit for "bytes were written". */
    size_t space_bit; /**< Event bit for "bytes were read, there is room". */
    PipeRole role;
} PipeEnd;

/**
 * @brief Makes a pipe and both of its ends, sharing one new event.
 *
 * @param[in]  size Ring size in bytes: a power of two, at least 64. PIPE_SIZE_DEFAULT is one page.
 * @param[out] w    The writer end.
 * @param[out] r    The reader end.
 * @return ZUZU_OK, ERR_BADARG for a bad @p size, or ERR_NOMEM.
 */
Err CreatePipe(size_t size, PipeEnd *w, PipeEnd *r);

/**
 * @brief Builds an end from handles the space was granted or received.
 *
 * Maps @p mem and reads the ring size from the page. Takes ownership of both handles: PipeClose()
 * closes them.
 *
 * @param[out] e         The end.
 * @param[in]  role      PIPE_WRITER or PIPE_READER.
 * @param[in]  mem       The page, with PERM_MAP | PERM_WRITE.
 * @param[in]  event     The event, with PERM_SEND, plus PERM_WAIT if this end ever blocks.
 * @param[in]  data_bit  Bit 0-30 for data. The event may be shared with other pipes.
 * @param[in]  space_bit Bit 0-30 for space; ignored if this end never blocks.
 * @return ZUZU_OK, ERR_BADARG, or the mapping error.
 */
Err OpenPipe(PipeEnd *e, PipeRole role, Handle mem, Handle event, size_t data_bit,
             size_t space_bit);

/**
 * @brief Duplicates an end's handles so they can be granted to another space.
 *
 * @param[in]  e          The end to share.
 * @param[in]  event_perms Rights for the event copy: PERM_SEND alone for an untrusted peer,
 *                        plus PERM_WAIT for a trusted one.
 * @param[out] mem        Receives the page copy (PERM_MAP | PERM_WRITE | PERM_TXFR).
 * @param[out] event      Receives the event copy.
 * @return ZUZU_OK, or the duplication error.
 */
Err PipeDupHandles(const PipeEnd *e, HandlePerms event_perms, Handle *mem, Handle *event);

/**
 * @brief Writes all @p len bytes, waiting for room.
 *
 * @param[in] w       The writer.
 * @param[in] buf     Bytes to send.
 * @param[in] len     How many.
 * @param[in] timeout TIMEOUT_POLL, TIMEOUT_INFINITE, or milliseconds.
 * @return Bytes written; fewer than @p len on timeout. A negative Err if nothing was written:
 *         ERR_DEAD if the reader closed (a broken pipe), or ERR_TIMEOUT.
 */
int PipeWrite(PipeEnd *w, const void *buf, size_t len, Duration timeout);

/**
 * @brief Writes all @p len bytes or none, and never blocks. For loggers.
 *
 * @param[in] w   The writer.
 * @param[in] buf Bytes to send.
 * @param[in] len How many.
 * @return ZUZU_OK, ERR_BUFFULL (counted in PipeDropped()), or ERR_DEAD if the reader closed.
 */
Err PipeTryWriteAll(PipeEnd *w, const void *buf, size_t len);

/**
 * @brief Reads up to @p cap bytes, waiting for the first one.
 *
 * @param[in]  r       The reader.
 * @param[out] buf     Where to store the bytes.
 * @param[in]  cap     Room in @p buf.
 * @param[in]  timeout TIMEOUT_POLL, TIMEOUT_INFINITE, or milliseconds.
 * @return Bytes read; 0 at EOF (writer closed and the ring is empty); ERR_TIMEOUT if nothing
 *         arrived in time.
 */
int PipeRead(PipeEnd *r, void *buf, size_t cap, Duration timeout);

/** @brief Bytes a reader could read now. */
uint32_t PipeAvail(const PipeEnd *r);
/** @brief Bytes a writer could write now. */
uint32_t PipeFree(const PipeEnd *w);
/** @brief PipeTryWriteAll() calls that found no room. */
uint32_t PipeDropped(const PipeEnd *e);
/** @brief Whether the other end has closed, or was marked closed. */
bool PipePeerClosed(const PipeEnd *e);

/**
 * @brief Marks an end closed on its behalf and wakes the peer.
 *
 *
 * @param[in] e     Any open end of this pipe.
 * @param[in] which PIPE_WRITER or PIPE_READER: the end that is gone.
 */
void PipeMarkClosed(PipeEnd *e, PipeRole which);

/**
 * @brief Closes this end by setting its closed flag, waking the peer, unmapping and closing the handles.
 *
 * @param[in] e The end; left unusable.
 */
void ClosePipe(PipeEnd *e);

#ifdef __cplusplus
}
#endif

#endif /* UTIL_PIPE_H */

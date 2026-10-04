#ifndef ZUZU_FSD_CLIENT_H
#define ZUZU_FSD_CLIENT_H

/**
 * fsd_client.h - minimal client for the fsd protocol.
 *
 * fsd requires the client to own the shared buffer: create it and grant it
 * to fsd with FSD_ATTACH, which returns a badged port for the session. This
 * header wraps that handshake and the per-command request/response
 * marshalling so callers (zzsh, ...) don't each re-implement it.
 */

#include <fs/protocols/fsd.h>
#include <stdbool.h>
#include <types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    Handle port;   /* badged handle to fsd's port (the session)    */
    Handle shm;    /* our shm handle                               */
    uint8_t *buf;  /* mapped base of the shared buffer             */
    uint32_t size; /* buffer size (page-aligned)                   */
    Handle live;   /* port we own; fsd watches it to notice our death */
    bool ready;
} FsdConn;

/**
 * @brief Attaches a client's shared buffer to an already-resolved fsd port.
 *
 * @param c The connection state to initialize.
 * @param port Unmarked handle to fsd's port; the caller keeps ownership.
 * @param pid Unused; kept for source compatibility.
 * @param want_size Requested shared buffer size; clamped to [FSD_SHM_MIN, FSD_SHM_MAX]
 * and page-aligned.
 * @return Err ZUZU_OK on success, or a negative error code on failure.
 */
Err FsdAttach(FsdConn *c, Handle port, Spid pid, uint32_t want_size);

/**
 * @brief Establishes a session by resolving fsd over the nameserver, then attaching.
 * For ordinary clients.
 *
 * @param c The connection state to initialize.
 * @param want_size Requested shared buffer size; see FsdAttach().
 * @return Err ZUZU_OK on success, or a negative error code on failure.
 */
Err FsdConnect(FsdConn *c, uint32_t want_size);

/**
 * @brief Ends the session: fsd closes the fds it owns and frees the session.
 *
 * @param c An attached connection.
 * @return Err ZUZU_OK on success, or a negative error code on failure.
 */
Err FsdDetach(FsdConn *c);

/**
 * @brief Opens a file by path.
 *
 * @param c An attached connection.
 * @param path Path to open.
 * @param mode FSD_MODE_* flags.
 * @param fd Out-param set to the opened file descriptor on success.
 * @return Err ZUZU_OK on success, or a negative error code on failure.
 */
Err FsdOpen(FsdConn *c, const char *path, uint32_t mode, uint32_t *fd);

/**
 * @brief Closes a file descriptor previously returned by FsdOpen().
 *
 * @param c An attached connection.
 * @param fd The file descriptor to close.
 * @return Err ZUZU_OK on success, or a negative error code on failure.
 */
Err FsdClose(FsdConn *c, uint32_t fd);

/**
 * @brief Reads up to `count` bytes from `fd` into `dst`.
 *
 * @param c An attached connection.
 * @param fd The file descriptor to read from.
 * @param dst Destination buffer; may be NULL to discard the data.
 * @param count Maximum number of bytes to read.
 * @param got Out-param set to the number of bytes actually read.
 * @return Err ZUZU_OK on success, or a negative error code on failure.
 */
Err FsdRead(FsdConn *c, uint32_t fd, void *dst, uint32_t count, uint32_t *got);

/**
 * @brief Writes up to `count` bytes from `src` to `fd`.
 *
 * @param c An attached connection.
 * @param fd The file descriptor to write to.
 * @param src Source buffer.
 * @param count Maximum number of bytes to write.
 * @param put Out-param set to the number of bytes actually written.
 * @return Err ZUZU_OK on success, or a negative error code on failure.
 */
Err FsdWrite(FsdConn *c, uint32_t fd, const void *src, uint32_t count, uint32_t *put);

/**
 * @brief Repositions the file offset of `fd`.
 *
 * @param c An attached connection.
 * @param fd The file descriptor.
 * @param offset Offset relative to `whence`.
 * @param whence An FsdWhence value.
 * @param newpos Out-param set to the new absolute position; may be NULL.
 * @return Err ZUZU_OK on success, or a negative error code on failure.
 */
Err FsdSeek(FsdConn *c, uint32_t fd, int64_t offset, uint32_t whence, int64_t *newpos);

/**
 * @brief Removes a file or empty directory.
 *
 * @param c An attached connection.
 * @param path Path to remove.
 * @return Err ZUZU_OK on success, or a negative error code on failure.
 */
Err FsdUnlink(FsdConn *c, const char *path);

/**
 * @brief Stats a file by path.
 *
 * @param c An attached connection.
 * @param path Path to stat.
 * @param st Out-param filled with the file's stat info.
 * @return Err ZUZU_OK on success, or a negative error code on failure.
 */
Err FsdGetStat(FsdConn *c, const char *path, FsdStat *st);

/**
 * @brief Stats an open file by descriptor.
 *
 * @param c An attached connection.
 * @param fd The file descriptor.
 * @param st Out-param filled with the file's stat info.
 * @return Err ZUZU_OK on success, or a negative error code on failure.
 */
Err FsdFstat(FsdConn *c, uint32_t fd, FsdStat *st);

/**
 * @brief Reads directory entries starting at `start`.
 *
 * @param c An attached connection.
 * @param path Directory path.
 * @param start Index of the first entry to return.
 * @param out Destination array of entries.
 * @param max Capacity of `out`, in entries.
 * @param count Out-param set to the number of entries actually returned.
 * @return Err ZUZU_OK on success, or a negative error code on failure.
 */
Err FsdReadDir(FsdConn *c, const char *path, uint32_t start, FsdDirEntry *out, uint32_t max,
               uint32_t *count);

#ifdef __cplusplus
}
#endif

#endif /* ZUZU_FSD_CLIENT_H */

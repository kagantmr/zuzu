#ifndef ZUZU_FSD_CLIENT_H
#define ZUZU_FSD_CLIENT_H

/**
 * @file
 * @brief Minimal client for the fsd protocol.
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

/** @brief A session with fsd. */
typedef struct {
    Handle port;   /**< Badged handle to fsd's port: the session. */
    Handle shm;    /**< The shared buffer's memory object. */
    uint8_t *buf;  /**< Mapped shared buffer. */
    uint32_t size; /**< Buffer size, page-aligned. */
    Handle live;   /**< Port this client owns; fsd watches it to notice the client's death. */
    bool ready;    /**< Whether the session is attached. */
} FsdConn;

/**
 * @brief Attaches a client's shared buffer to an already-resolved fsd port.
 *
 * @param[in,out] c The connection state to initialize.
 * @param[in] port Unmarked handle to fsd's port; the caller keeps ownership.
 * @param[in] pid Unused; kept for source compatibility.
 * @param[in] want_size Requested shared buffer size; clamped to [FSD_SHM_MIN, FSD_SHM_MAX]
 * and page-aligned.
 * @return ZUZU_OK, or an error code.
 */
Err FsdAttach(FsdConn *c, Handle port, Spid pid, uint32_t want_size);

/**
 * @brief Establishes a session by resolving fsd over the nameserver, then attaching.
 * For ordinary clients.
 *
 * @param[in,out] c The connection state to initialize.
 * @param[in] want_size Requested shared buffer size; see FsdAttach().
 * @return ZUZU_OK, or an error code.
 */
Err FsdConnect(FsdConn *c, uint32_t want_size);

/**
 * @brief Ends the session: fsd closes the fds it owns and frees the session.
 *
 * @param[in,out] c An attached connection.
 * @return ZUZU_OK, or an error code.
 */
Err FsdDetach(FsdConn *c);

/**
 * @brief Opens a file by path.
 *
 * @param[in,out] c An attached connection.
 * @param[in] path Path to open.
 * @param[in] mode FSD_MODE_* flags.
 * @param[out] fd Receives the opened file descriptor on success.
 * @return ZUZU_OK, or an error code.
 */
Err FsdOpen(FsdConn *c, const char *path, uint32_t mode, uint32_t *fd);

/**
 * @brief Closes a file descriptor previously returned by FsdOpen().
 *
 * @param[in,out] c An attached connection.
 * @param[in] fd The file descriptor to close.
 * @return ZUZU_OK, or an error code.
 */
Err FsdClose(FsdConn *c, uint32_t fd);

/**
 * @brief Reads up to @p count bytes from @p fd into @p dst.
 *
 * @param[in,out] c An attached connection.
 * @param[in] fd The file descriptor to read from.
 * @param[out] dst Destination buffer; may be NULL to discard the data.
 * @param[in] count Maximum number of bytes to read.
 * @param[out] got Receives the number of bytes actually read.
 * @return ZUZU_OK, or an error code.
 */
Err FsdRead(FsdConn *c, uint32_t fd, void *dst, uint32_t count, uint32_t *got);

/**
 * @brief Reads up to @p count bytes from @p fd straight into a shm object, at byte offset
 * @p obj_off, with no copy through the session buffer.
 *
 * @param[in,out] c An attached connection.
 * @param[in] fd The file descriptor to read from.
 * @param[in] obj A memory object you own; fsd gets a PERM_MAP | PERM_WRITE copy for the call.
 * @param[in] obj_off Byte offset into the object where the data goes.
 * @param[in] count Maximum number of bytes to read; obj_off + count must fit in the object.
 * @param[out] got Receives the number of bytes actually read.
 * @return ZUZU_OK, or an error code.
 */
Err FsdReadObj(FsdConn *c, uint32_t fd, Handle obj, uint32_t obj_off, uint32_t count,
               uint32_t *got);

/**
 * @brief Writes up to @p count bytes from @p src to @p fd.
 *
 * @param[in,out] c An attached connection.
 * @param[in] fd The file descriptor to write to.
 * @param[in] src Source buffer.
 * @param[in] count Maximum number of bytes to write.
 * @param[out] put Receives the number of bytes actually written.
 * @return ZUZU_OK, or an error code.
 */
Err FsdWrite(FsdConn *c, uint32_t fd, const void *src, uint32_t count, uint32_t *put);

/**
 * @brief Repositions the file offset of @p fd.
 *
 * @param[in,out] c An attached connection.
 * @param[in] fd The file descriptor.
 * @param[in] offset Offset relative to @p whence.
 * @param[in] whence An FsdWhence value.
 * @param[out] newpos Receives the new absolute position; may be NULL.
 * @return ZUZU_OK, or an error code.
 */
Err FsdSeek(FsdConn *c, uint32_t fd, int64_t offset, uint32_t whence, int64_t *newpos);

/**
 * @brief Removes a file or empty directory.
 *
 * @param[in,out] c An attached connection.
 * @param[in] path Path to remove.
 * @return ZUZU_OK, or an error code.
 */
Err FsdUnlink(FsdConn *c, const char *path);

/**
 * @brief Stats a file by path.
 *
 * @param[in,out] c An attached connection.
 * @param[in] path Path to stat.
 * @param[out] st Receives the file's stat info.
 * @return ZUZU_OK, or an error code.
 */
Err FsdGetStat(FsdConn *c, const char *path, FsdStat *st);

/**
 * @brief Stats an open file by descriptor.
 *
 * @param[in,out] c An attached connection.
 * @param[in] fd The file descriptor.
 * @param[out] st Receives the file's stat info.
 * @return ZUZU_OK, or an error code.
 */
Err FsdFstat(FsdConn *c, uint32_t fd, FsdStat *st);

/**
 * @brief Reads directory entries starting at @p start.
 *
 * @param[in,out] c An attached connection.
 * @param[in] path Directory path.
 * @param[in] start Index of the first entry to return.
 * @param[out] out Destination array of entries.
 * @param[in] max Capacity of @p out, in entries.
 * @param[out] count Receives the number of entries actually returned.
 * @return ZUZU_OK, or an error code.
 */
Err FsdReadDir(FsdConn *c, const char *path, uint32_t start, FsdDirEntry *out, uint32_t max,
               uint32_t *count);

/**
 * @brief Reads a whole regular file into a fresh shm object, e.g. a program image for
 * SpawnProcessObj().
 *
 * @param[in,out] c An attached connection.
 * @param[in] path Path of the file to read.
 * @param[out] obj Receives the new object's handle on success; the caller closes it.
 * @param[out] len Receives the file size in bytes on success.
 * @return ZUZU_OK, or an error code.
 * @retval ERR_BADARG @p path is not a non-empty regular file.
 * @retval ERR_IO     The file ended before its stated size.
 */
Err FsdReadImage(FsdConn *c, const char *path, Handle *obj, size_t *len);

#ifdef __cplusplus
}
#endif

#endif /* ZUZU_FSD_CLIENT_H */

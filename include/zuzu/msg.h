/**
 * @file msg.h
 * @brief Message passing convenience functions.
 */

#ifndef ZUZU_MSG_H
#define ZUZU_MSG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <util/tls.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <types.h>
#include <zuzu/err.h>
#include <zuzu/zuzu.h>

/**
 * @brief Getter for the per-task message buffer.
 *
 * @return Pointer to the MSG_BUF_SIZE-byte buffer.
 */
static inline void *GetMessageBox(void) { return GetTls()->msg_buf; }

/**
 * @brief Read @p len bytes from @p src and write it into the message buffer
 * 
 * @param[in] src The buffer to read from
 * @param[in] len The amount of bytes to read
 * @return The number of bytes written on success, negative on failure.
 */
static inline Err MsgWrite(const void *src, size_t len)
{
    if (len > MSG_BUF_SIZE)
        return ERR_OVERFLOW;
    memcpy(GetMessageBox(), src, len);
    return (Err)len;
}

/**
 * @brief Read @p len bytes from the message buffer and write it into @p dst
 *
 * @param[out] dst The buffer to write to
 * @param[in] len The amount of bytes to read
 * @return The number of bytes written on success, negative on failure.
 */
static inline Err MsgRead(void *dst, size_t len)
{
    if (len > MSG_BUF_SIZE)
        return ERR_OVERFLOW;
    memcpy(dst, GetMessageBox(), len);
    return (Err)len;
}

/**
 * @brief Message passing writer to keep track of the buffer.
 */
typedef struct {
    uint32_t off; /**< The current offset. Kept as a cursor */
    uint32_t cap; /**< The size of the buffer. */
    bool ovf; /**< Is the buffer size exceeded? */
} MsgWriter;

/**
 * @brief Initializes a message writer to default values.
 * 
 * @param[in] w The writer to initialize.
 */
static inline void MsgWriterInit(MsgWriter *w)
{
    w->off = 0;
    w->cap = MSG_BUF_SIZE;
    w->ovf = false;
}

/**
 * @brief Put a 4-byte word into the message buffer.
 * 
 * @param[in] w The writer to keep track.
 * @param[in] v The value to write
 */
static inline void MsgPutU32(MsgWriter *w, uint32_t v)
{
    if (w->off + 4 > w->cap) {
        w->ovf = true;
        return;
    }
    memcpy((char *)GetMessageBox() + w->off, &v, 4);
    w->off += 4;
}

/**
 * @brief Put one byte into the message buffer.
 *
 * Sets the overflow flag instead of writing when the byte does not fit.
 *
 * @param[in] w The writer to keep track.
 * @param[in] v The value to write
 */
static inline void MsgPutByte(MsgWriter *w, uint8_t v)
{
    if (w->off + 1 > w->cap) {
        w->ovf = true;
        return;
    }
    *((char *)GetMessageBox() + w->off) = v;
    w->off += 1;
}

/**
 * @brief Put a NUL-terminated string, terminator included, into the message buffer.
 *
 * Sets the overflow flag instead of writing when the string and its NUL do not fit.
 *
 * @param[in] w The writer to keep track.
 * @param[in] s The string to write
 */
static inline void MsgPutStr(MsgWriter *w, const char *s)
{
    uint32_t n = (uint32_t)strlen(s) + 1;
    if (w->off + n > w->cap) {
        w->ovf = true;
        return;
    }
    memcpy((char *)GetMessageBox() + w->off, s, n);
    w->off += n;
}

/**
 * @brief Get the number of bytes written so far.
 *
 * @param[in] w The writer to query.
 * @return The cursor position, which is the length to pass to Call() or Reply().
 */
static inline uint32_t MsgWriterLen(const MsgWriter *w) { return w->off; }

/**
 * @brief Check that nothing written so far overflowed the buffer.
 *
 * @param[in] w The writer to query.
 * @return true if no put has overflowed, false otherwise.
 */
static inline bool MsgWriterOk(const MsgWriter *w) { return !w->ovf; }

/**
 * @brief Claim the next @p n bytes of the message buffer.
 *
 * Sets the overflow flag and leaves the cursor alone when the bytes do not fit, or when the
 * writer has already overflowed.
 *
 * @param[in] w The writer to keep track.
 * @param[in] n The number of bytes to claim
 * @return Pointer to the claimed bytes, or NULL on overflow.
 */
static inline void *MsgReserve(MsgWriter *w, uint32_t n)
{
    if (w->ovf || n > w->cap - w->off) {
        w->ovf = true;
        return NULL;
    }
    void *p = (char *)GetMessageBox() + w->off;
    w->off += n;
    return p;
}

/**
 * @brief Put a 2-byte word into the message buffer.
 *
 * @param[in] w The writer to keep track.
 * @param[in] v The value to write
 */
static inline void MsgPutU16(MsgWriter *w, uint16_t v)
{
    void *p = MsgReserve(w, 2);
    if (p)
        memcpy(p, &v, 2);
}

/**
 * @brief Put an 8-byte word into the message buffer.
 *
 * @param[in] w The writer to keep track.
 * @param[in] v The value to write
 */
static inline void MsgPutU64(MsgWriter *w, uint64_t v)
{
    void *p = MsgReserve(w, 8);
    if (p)
        memcpy(p, &v, 8);
}

/**
 * @brief Put @p n raw bytes into the message buffer.
 *
 * @param[in] w The writer to keep track.
 * @param[in] src The bytes to write
 * @param[in] n The amount of bytes to write
 */
static inline void MsgPutBytes(MsgWriter *w, const void *src, uint32_t n)
{
    void *p = MsgReserve(w, n);
    if (p && n)
        memcpy(p, src, n);
}

/**
 * @brief Put the bytes of a variable, usually a struct, into the message buffer.
 *
 * @param w The writer to keep track.
 * @param obj The variable to write; must be an lvalue.
 */
#define MsgPutObj(w, obj) MsgPutBytes((w), &(obj), (uint32_t)sizeof(obj))

/**
 * @brief Pad the message with zero bytes up to a multiple of @p align.
 *
 * @param[in] w The writer to keep track.
 * @param[in] align The boundary to pad to; must be a power of two.
 */
static inline void MsgAlign(MsgWriter *w, uint32_t align)
{
    uint32_t pad = (align - (w->off & (align - 1))) & (align - 1);
    void *p = MsgReserve(w, pad);
    if (p && pad)
        memset(p, 0, pad);
}

/**
 * @brief Message passing reader that bounds every read to the received length.
 */
typedef struct {
    uint32_t off; /**< The current offset. Kept as a cursor */
    uint32_t len; /**< The number of valid bytes in the message. */
    bool err; /**< Has a read run past the end or failed? Once set, it stays set. */
} MsgReader;

/**
 * @brief Initializes a message reader over the first @p len bytes of the message buffer.
 *
 * Sets the error flag if @p len is larger than the buffer.
 *
 * @param[in] r The reader to initialize.
 * @param[in] len The length of the received message, as reported by the call or receive
 */
static inline void MsgReaderInit(MsgReader *r, uint32_t len)
{
    r->off = 0;
    r->len = len > MSG_BUF_SIZE ? MSG_BUF_SIZE : len;
    r->err = len > MSG_BUF_SIZE;
}

/**
 * @brief Check that every read so far succeeded.
 *
 * @param[in] r The reader to query.
 * @return true if no read has failed, false otherwise.
 */
static inline bool MsgReaderOk(const MsgReader *r) { return !r->err; }

/**
 * @brief Get the number of unread bytes.
 *
 * @param[in] r The reader to query.
 * @return The unread byte count, or 0 once the reader has failed.
 */
static inline uint32_t MsgReaderLeft(const MsgReader *r) { return r->err ? 0 : r->len - r->off; }

/**
 * @brief Claim the next @p n bytes of the message.
 *
 * Sets the error flag and leaves the cursor alone when fewer than @p n bytes remain.
 *
 * @param[in] r The reader to keep track.
 * @param[in] n The number of bytes to claim
 * @return Pointer into the message buffer, or NULL on failure.
 */
static inline const void *MsgTake(MsgReader *r, uint32_t n)
{
    if (r->err || n > r->len - r->off) {
        r->err = true;
        return NULL;
    }
    const void *p = (const char *)GetMessageBox() + r->off;
    r->off += n;
    return p;
}

/**
 * @brief Read a 4-byte word from the message.
 *
 * @param[in] r The reader to keep track.
 * @return The value, or 0 on failure.
 */
static inline uint32_t MsgGetU32(MsgReader *r)
{
    uint32_t v = 0;
    const void *p = MsgTake(r, 4);
    if (p)
        memcpy(&v, p, 4);
    return v;
}

/**
 * @brief Read a 2-byte word from the message.
 *
 * @param[in] r The reader to keep track.
 * @return The value, or 0 on failure.
 */
static inline uint16_t MsgGetU16(MsgReader *r)
{
    uint16_t v = 0;
    const void *p = MsgTake(r, 2);
    if (p)
        memcpy(&v, p, 2);
    return v;
}

/**
 * @brief Read an 8-byte word from the message.
 *
 * @param[in] r The reader to keep track.
 * @return The value, or 0 on failure.
 */
static inline uint64_t MsgGetU64(MsgReader *r)
{
    uint64_t v = 0;
    const void *p = MsgTake(r, 8);
    if (p)
        memcpy(&v, p, 8);
    return v;
}

/**
 * @brief Read one byte from the message.
 *
 * @param[in] r The reader to keep track.
 * @return The value, or 0 on failure.
 */
static inline uint8_t MsgGetByte(MsgReader *r)
{
    const uint8_t *p = (const uint8_t *)MsgTake(r, 1);
    return p ? *p : 0;
}

/**
 * @brief Copy @p n bytes out of the message.
 *
 * @param[in] r The reader to keep track.
 * @param[out] dst The buffer to write to
 * @param[in] n The amount of bytes to read
 * @return true on success, false if fewer than @p n bytes remain.
 */
static inline bool MsgGetBytes(MsgReader *r, void *dst, uint32_t n)
{
    const void *p = MsgTake(r, n);
    if (!p)
        return false;
    if (n)
        memcpy(dst, p, n);
    return true;
}

/**
 * @brief Read the bytes of a variable, usually a struct, out of the message.
 *
 * @param r The reader to keep track.
 * @param obj The variable to fill; must be an lvalue.
 * @return true on success, false if the message is too short.
 */
#define MsgGetObj(r, obj) MsgGetBytes((r), &(obj), (uint32_t)sizeof(obj))

/**
 * @brief Read a NUL-terminated string from the message without copying it.
 *
 * Fails if no NUL appears inside the received length, so the result is always safe to use
 * as a C string.
 *
 * @param[in] r The reader to keep track.
 * @return Pointer into the message buffer, or NULL on failure. Valid until the buffer is reused.
 */
static inline const char *MsgGetStr(MsgReader *r)
{
    if (r->err)
        return NULL;
    const char *s = (const char *)GetMessageBox() + r->off;
    uint32_t left = r->len - r->off;
    uint32_t n = 0;
    while (n < left && s[n] != '\0')
        n++;
    if (n == left) {
        r->err = true;
        return NULL;
    }
    r->off += n + 1;
    return s;
}

/**
 * @brief Skip @p n bytes of the message.
 *
 * @param[in] r The reader to keep track.
 * @param[in] n The amount of bytes to skip
 */
static inline void MsgSkip(MsgReader *r, uint32_t n) { (void)MsgTake(r, n); }

/**
 * @brief Skip bytes until the cursor is a multiple of @p align.
 *
 * @param[in] r The reader to keep track.
 * @param[in] align The boundary to skip to; must be a power of two.
 */
static inline void MsgReaderAlign(MsgReader *r, uint32_t align)
{
    MsgSkip(r, (align - (r->off & (align - 1))) & (align - 1));
}

/**
 * @brief Send a request and read the status word of the reply.
 *
 * Copies @p req into the message buffer, calls @p port, and checks that the reply holds at
 * least a status word. The rest of the reply stays in the message buffer for the caller.
 *
 * @param[in] port Port handle with PERM_SEND.
 * @param[in] req The request bytes
 * @param[in] len The request length in bytes
 * @param[in] grant Handle to copy to the server, or -1.
 * @param[out] granted Receives a handle granted back, or -1; may be NULL.
 * @return The status word from the reply, ERR_OVERFLOW if @p len is too large, ERR_MALFORMED if
 * the reply is shorter than a status word, or the error from the call.
 */
static inline Err MsgCallStatus(Handle port, const void *req, uint32_t len, Handle grant,
                                Handle *granted)
{
    if (granted)
        *granted = -1;
    Err wrote = MsgWrite(req, len);
    if (wrote < 0)
        return wrote;
    uint32_t got = 0;
    Handle back = -1;
    Err rc = CallPort(port, len, grant, &got, &back);
    if (granted)
        *granted = back;
    if (rc != ZUZU_OK)
        return rc;
    Err status;
    if (got < sizeof(status))
        return ERR_MALFORMED;
    memcpy(&status, GetMessageBox(), sizeof(status));
    return status;
}

#ifdef __cplusplus
}
#endif
#endif // ZUZU_MSG_H

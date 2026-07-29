#ifndef LMSG_H
#define LMSG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <string.h>
#include "tls.h"
#include <zuzu/types.h>

/**
 * @brief Accessor for the current thread's local message buffer.
 * 
 * @return void* Pointer to the local message buffer.
 * 
 */
static inline void *LmsgBuf(void) {
    return ZuzuTLS()->LmsgBuf;
}

/**
 * @brief Writes data to the current thread's local message buffer.
 * 
 * @param src Pointer to the source data to write.
 * @param len Length of the data to write in bytes.
 * 
 * @return Err Returns the number of bytes written on success, or a negative error code on failure (e.g., ERR_OVERFLOW if len exceeds LMSG_BUF_SIZE).
 */
static inline Err LmsgWrite(const void *src, size_t len) {
    if (len > LMSG_BUF_SIZE)
        return ERR_OVERFLOW;
    memcpy(LmsgBuf(), src, len);
    return len;
}

/**
 * @brief Reads data from the current thread's local message buffer.
 * 
 * @param dst Pointer to the destination buffer where the data will be read into.
 * @param len Length of the data to read in bytes.
 * 
 * @return Err Returns the number of bytes read on success, or a negative error code on failure (e.g., ERR_OVERFLOW if len exceeds LMSG_BUF_SIZE).
 */
static inline Err LmsgRead(void *dst, size_t len) {
    if (len > LMSG_BUF_SIZE)
        return ERR_OVERFLOW;
    memcpy(dst, LmsgBuf(), len);
    return len;
}

#ifdef __cplusplus
}
#endif

#endif // LMSG_H

#ifndef DEVMGR_PROTOCOL_H
#define DEVMGR_PROTOCOL_H

#include <stdint.h>
#include <zuzu/lmsg.h>
#include <zuzu/msg.h>
#include <zuzu/types.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DEVM_REQUEST
} DevmRequestType;


#define DEVM_COMPAT_MAX 32
#define DEVM_MAX_COMPAT 8   /* cap so strings[] is bounded */

typedef struct {
    DevmRequestType cmd;
    uint32_t        count;
    const char     *strings[DEVM_MAX_COMPAT];  /* point into the lmsg buf */
} DevmRequest;

/* Error codes (ERR_NOENT, ERR_NOSYS, ERR_NOMEM, ...) live in <zuzu/err.h> */

/**
 * 
 */
static inline Handle DevmRequestDevice(Handle devmgr_port,
                                       const char *const *compats,
                                       uint32_t count,
                                       uint32_t *out_matched)
{
    if (count == 0 || count > DEVM_MAX_COMPAT)
        return ERR_BADARG;

    char *b = LmsgBuf();
    uint32_t cmd = DEVM_REQUEST;
    memcpy(b + 0, &cmd,   4);
    memcpy(b + 4, &count, 4);

    uint32_t off = 8;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t n = (uint32_t)strlen(compats[i]) + 1;   /* incl NUL */
        if (off + n > LMSG_BUF_SIZE)
            return ERR_OVERFLOW;
        memcpy(b + off, compats[i], n);
        off += n;
    }

    Message m = ZuzuMsgLcall(devmgr_port, off);
    if ((int32_t)m.w1 < 0)                 /* status is w1, not w0 */
        return (Handle)(int32_t)m.w1;
    if (out_matched)
        *out_matched = m.w3;               /* matched index is w3, not w2 */
    return (Handle)m.w2;                   /* granted handle is w2, not w1 */
}

#ifdef __cplusplus
}
#endif

#endif

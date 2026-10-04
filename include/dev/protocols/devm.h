#ifndef DEVMGR_PROTOCOL_H
#define DEVMGR_PROTOCOL_H

#include <stdint.h>
#include <string.h>
#include <types.h>
#include <util/msg.h>
#include <zuzu/zuzu.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { DEVM_REQUEST } DevmRequestType;

#define DEVM_COMPAT_MAX 32
#define DEVM_MAX_COMPAT 8 /* cap so strings[] is bounded */

typedef struct {
    DevmRequestType cmd;
    uint32_t count;
    const char *strings[DEVM_MAX_COMPAT]; /* point into the lmsg buf */
} DevmRequest;

static inline Handle RequestDevice(Handle devsvc_port, const char *const *compats, uint32_t count,
                                   uint32_t *out_matched)
{
    if (count == 0 || count > DEVM_MAX_COMPAT)
        return ERR_BADARG;

    char *b = MessageBuf();
    uint32_t cmd = DEVM_REQUEST;
    memcpy(b + 0, &cmd, 4);
    memcpy(b + 4, &count, 4);

    uint32_t off = 8;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t n = (uint32_t)strlen(compats[i]) + 1;
        if (off + n > MSG_BUF_SIZE)
            return ERR_OVERFLOW;
        memcpy(b + off, compats[i], n);
        off += n;
    }

    SvcResult r = Call(devsvc_port, off, -1);
    if (r.r0 != ZUZU_OK)
        return (Handle)r.r0;

    if (out_matched)
        memcpy(out_matched, MessageBuf(), sizeof(uint32_t));

    return (Handle)r.r3;
}

#ifdef __cplusplus
}
#endif

#endif

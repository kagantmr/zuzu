#ifndef FB_PROTOCOL_H
#define FB_PROTOCOL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <zuzu/err.h>

/*
 * Framebuffer protocol served by gfxsvc ("/svc/gfx").
 */

#define FB_CMD_GET_INFO 1 /* {cmd}. reply FbInfo */
#define FB_CMD_GET_BUF                                                                             \
    2 /* grant: a port the caller owns (PERM_WAIT|PERM_TXFR); the driver drops ownership when it   \
         dies. {cmd}. reply FbInfo and GRANTS a PERM_MAP|PERM_TXFR dup of the framebuffer.         \
         ERR_BUSY if someone else owns it. */
#define FB_CMD_RELEASE 3 /* {cmd, token}. reply {Err status} */
#define FB_CMD_SET_EVENT                                                                           \
    4 /* grant: the caller's own event (PERM_SEND|PERM_TXFR). {cmd, token}. The driver      \
         signals FB_EVT_* bits on it. Replaces any previous event. */
#define FB_CMD_FLIP                                                                                \
    5 /* {cmd, token, arg = image index}. Scanout switches to that image at the start of the  \
         next frame; the reply does not wait for that. FB_EVT_FLIPPED fires once it has, which     \
         means the previously shown image is free to draw into. ERR_BUSY if a flip is pending,     \
         ERR_BADARG for a bad index. */
#define FB_CMD_SET_VSYNC                                                                           \
    6 /* {cmd, token, arg = 0|1}. While enabled the driver signals FB_EVT_VSYNC every frame. */

#define FB_EVT_VSYNC (1u << 0)
#define FB_EVT_FLIPPED (1u << 1)

#define FB_FMT_RGB565 1u /* R in bits 15..11, G in 10..5, B in 4..0 */

typedef struct {
    uint32_t cmd;
    uint32_t token;
    uint32_t arg;
} FbRequest;

typedef struct {
    Err status;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t format;
    uint32_t buffers;  /* images in the MemObject, 1 = no double buffering */
    uint32_t buf_size; /* bytes per image, a whole number of pages */
    uint32_t token;    /* GET_BUF only: ownership token, 0 otherwise */
} FbInfo;

#ifdef __cplusplus
}
#endif

#endif /* FB_PROTOCOL_H */

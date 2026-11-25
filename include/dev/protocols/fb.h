/**
 * @file
 * @brief Framebuffer protocol served by gfxsvc ("/svc/gfx").
 *
 * Each client asks for the framebuffer with FB_CMD_GET_BUF and gets a virtual terminal (VT). Only
 * the active VT may draw and flip. Switching is cooperative: gfxsvc signals FB_EVT_DEACTIVATE to
 * the active client, which stops drawing and answers FB_CMD_DEACTIVATED (or is cut off after
 * FB_SWITCH_TIMEOUT_MS), then signals FB_EVT_ACTIVATE to the new one, which redraws everything.
 */

#ifndef FB_PROTOCOL_H
#define FB_PROTOCOL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <zuzu/err.h>

/** @brief Ask for the display's geometry. Request {cmd}; replies FbInfo. */
#define FB_CMD_GET_INFO 1
/**
 * @brief Take ownership of the framebuffer.
 *
 * Request {cmd}, granting a port the caller owns (PERM_WAIT | PERM_TXFR); the VT is freed when
 * that port dies. Replies FbInfo and grants a PERM_MAP | PERM_WRITE | PERM_TXFR duplicate of the
 * framebuffer. The first client is active at once; later ones start inactive. ERR_BUSY if all
 * FB_MAX_VTS are taken.
 */
#define FB_CMD_GET_BUF 2
/** @brief Give up ownership. Request {cmd, token}; replies {Err status}. */
#define FB_CMD_RELEASE 3
/**
 * @brief Give gfxsvc an event to signal. Request {cmd, token, arg = shift}, granting the caller's
 * own event (PERM_SEND | PERM_TXFR). gfxsvc signals FB_EVT_* bits shifted left by @c arg on it.
 * Replaces any previous event. An active client is sent FB_EVT_ACTIVATE straight away.
 */
#define FB_CMD_SET_EVENT 4
/**
 * @brief Show another image. Request {cmd, token, arg = image index}.
 *
 * Scanout switches to that image at the start of the next frame, and the reply does not wait for
 * that. FB_EVT_FLIPPED fires once it has, which means the previously shown image is free to draw
 * into. ERR_BUSY if a flip is pending or the caller is not the active VT, ERR_BADARG for a bad
 * index.
 */
#define FB_CMD_FLIP 5
/**
 * @brief Turn vertical-sync events on or off. Request {cmd, token, arg = 0 or 1}.
 *
 * While enabled the driver signals FB_EVT_VSYNC every frame. ERR_BUSY unless the caller is the
 * active VT.
 */
#define FB_CMD_SET_VSYNC 6
/**
 * @brief Make another VT active. Request {cmd, token ignored, arg = VT number}.
 *
 * Replies once the switch has started; it completes asynchronously. ERR_NOENT if no client holds
 * that VT, ERR_BUSY if another switch is still in progress.
 */
#define FB_CMD_SWITCH 7
/** @brief Answer FB_EVT_DEACTIVATE: the caller has stopped drawing. Request {cmd, token}. */
#define FB_CMD_DEACTIVATED 8

/** @brief Event bit: a frame was scanned out (while FB_CMD_SET_VSYNC is on). */
#define FB_EVT_VSYNC (1u << 0)
/** @brief Event bit: a requested flip took effect. */
#define FB_EVT_FLIPPED (1u << 1)
/** @brief Event bit: stop drawing and send FB_CMD_DEACTIVATED. */
#define FB_EVT_DEACTIVATE (1u << 2)
/** @brief Event bit: this VT is on screen again; redraw everything. */
#define FB_EVT_ACTIVATE (1u << 3)
/** @brief Every FB_EVT_* bit. */
#define FB_EVT_ALL (FB_EVT_VSYNC | FB_EVT_FLIPPED | FB_EVT_DEACTIVATE | FB_EVT_ACTIVATE)

/** @brief Most clients gfxsvc serves at once; VTs are numbered 1 to this. */
#define FB_MAX_VTS 8u
/** @brief How long the active client has to answer FB_EVT_DEACTIVATE. */
#define FB_SWITCH_TIMEOUT_MS 500u

/** @brief Pixel format: 16 bits per pixel, red in bits 15 to 11, green in 10 to 5, blue in 4 to 0. */
#define FB_FMT_RGB565 1u

/** @brief A request to gfxsvc. */
typedef struct {
    uint32_t cmd;   /**< An FB_CMD_* value. */
    uint32_t token; /**< Ownership token from FB_CMD_GET_BUF; 0 for FB_CMD_GET_INFO. */
    uint32_t arg;   /**< Command argument, if the command takes one. */
} FbRequest;

/** @brief gfxsvc's reply to FB_CMD_GET_INFO and FB_CMD_GET_BUF. */
typedef struct {
    Err status;        /**< ZUZU_OK, or an error code. */
    uint32_t width;    /**< Width in pixels. */
    uint32_t height;   /**< Height in pixels. */
    uint32_t stride;   /**< Bytes from one row to the next. */
    uint32_t format;   /**< An FB_FMT_* value. */
    uint32_t buffers;  /**< Images in the memory object; 1 means no double buffering. */
    uint32_t buf_size; /**< Bytes per image, a whole number of pages. */
    uint32_t token;    /**< FB_CMD_GET_BUF only: ownership token; 0 otherwise. */
    uint32_t vt;       /**< FB_CMD_GET_BUF: the caller's VT. FB_CMD_GET_INFO: the active VT, 0 if none. */
    uint32_t active;   /**< FB_CMD_GET_BUF only: non-zero if the caller's VT is active. */
} FbInfo;

#ifdef __cplusplus
}
#endif

#endif /* FB_PROTOCOL_H */

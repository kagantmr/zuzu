/**
 * @file
 * @brief Framebuffer protocol served by gfxsvc ("/svc/gfx").
 *
 * One client at a time owns the display. It asks for the framebuffer with FB_CMD_GET_BUF,
 * draws into the mapped images, and shows one with FB_CMD_FLIP.
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
 * Request {cmd}, granting a port the caller owns (PERM_WAIT | PERM_TXFR); the display drops
 * ownership when that port dies. Replies FbInfo and grants a PERM_MAP | PERM_WRITE | PERM_TXFR
 * duplicate of the framebuffer. ERR_BUSY if someone else owns it.
 */
#define FB_CMD_GET_BUF 2
/** @brief Give up ownership. Request {cmd, token}; replies {Err status}. */
#define FB_CMD_RELEASE 3
/**
 * @brief Give gfxsvc an event to signal. Request {cmd, token}, granting the caller's own event
 * (PERM_SEND | PERM_TXFR). The driver signals FB_EVT_* bits on it. Replaces any previous event.
 */
#define FB_CMD_SET_EVENT 4
/**
 * @brief Show another image. Request {cmd, token, arg = image index}.
 *
 * Scanout switches to that image at the start of the next frame, and the reply does not wait for
 * that. FB_EVT_FLIPPED fires once it has, which means the previously shown image is free to draw
 * into. ERR_BUSY if a flip is pending, ERR_BADARG for a bad index.
 */
#define FB_CMD_FLIP 5
/**
 * @brief Turn vertical-sync events on or off. Request {cmd, token, arg = 0 or 1}.
 *
 * While enabled the driver signals FB_EVT_VSYNC every frame.
 */
#define FB_CMD_SET_VSYNC 6

/** @brief Event bit: a frame was scanned out (while FB_CMD_SET_VSYNC is on). */
#define FB_EVT_VSYNC (1u << 0)
/** @brief Event bit: a requested flip took effect. */
#define FB_EVT_FLIPPED (1u << 1)

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
} FbInfo;

#ifdef __cplusplus
}
#endif

#endif /* FB_PROTOCOL_H */

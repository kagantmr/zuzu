/**
 * @file
 * @brief Client side of the window manager protocol (dev/protocols/wm.h).
 *
 * WinConnect() finds wm and registers the process; WinCreate() opens a window backed by a surface
 * the library allocates and maps. Draw into Win.px (RGB565, Win.width pixels per row), then call
 * WinDamage() for the area that changed. wm signals the window's event bits (WM_EVT_* shifted by
 * the shift given to WinCreate()) when input arrives, a frame is shown or the user closes it.
 */

#ifndef ZGFX_WIN_H
#define ZGFX_WIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include <dev/protocols/input.h>
#include <dev/protocols/wm.h>
#include <stdint.h>
#include <types.h>

/** @brief A connection to wm. */
typedef struct {
    Handle port;       /**< wm's service port. */
    Handle live;       /**< Port wm watches to notice this process is gone. */
    uint32_t token;    /**< Identifies this client in requests. */
    uint32_t screen_w; /**< Screen width in pixels. */
    uint32_t screen_h; /**< Screen height in pixels. */
} WinConn;

/** @brief Where and how WinCreate() opens a window. */
typedef struct {
    uint32_t width;     /**< Surface width in pixels. */
    uint32_t height;    /**< Surface height in pixels. */
    uint32_t layer;     /**< A WM_LAYER_* value. */
    int32_t x;          /**< Desktop and panel layers: left edge on screen. */
    int32_t y;          /**< Desktop and panel layers: top edge on screen. */
    const char *title;  /**< Window title. */
} WinSpec;

/** @brief An open window. */
typedef struct {
    WinConn *conn;    /**< The connection it belongs to. */
    uint32_t id;      /**< wm's id for the window. */
    uint32_t width;   /**< Surface width in pixels. */
    uint32_t height;  /**< Surface height in pixels. */
    uint16_t *px;     /**< The surface: width * height RGB565 pixels. */
    Handle mem;       /**< Memory object behind @c px. */
    InputRing *ring;  /**< Input for this window, or NULL before WinCreate() succeeds. */
    Handle ring_mem;  /**< Memory object behind @c ring. */
} Win;

/**
 * @brief Connects to wm, waiting up to @p wait_ms for it to appear.
 *
 * @param[out] conn    Filled in on success.
 * @param[in]  wait_ms How long to keep looking for /svc/wm.
 * @return ZUZU_OK, ERR_NOENT if wm never appeared, or an error from the request.
 */
Err WinConnect(WinConn *conn, uint32_t wait_ms);

/**
 * @brief Opens a window.
 *
 * @param[in]  conn  Connection from WinConnect().
 * @param[in]  spec  Size, layer, position and title.
 * @param[in]  event Event wm signals for this window.
 * @param[in]  shift Bit position of WM_EVT_INPUT in @p event; WM_EVT_BITS bits are used.
 * @param[out] win   Filled in on success.
 * @return ZUZU_OK or an error.
 */
Err WinCreate(WinConn *conn, const WinSpec *spec, Handle event, uint32_t shift, Win *win);

/**
 * @brief Tells wm an area of the surface changed.
 *
 * @param[in] win    The window.
 * @param[in] x      Left edge of the area.
 * @param[in] y      Top edge of the area.
 * @param[in] width  Width of the area.
 * @param[in] height Height of the area.
 * @return ZUZU_OK or an error.
 */
Err WinDamage(Win *win, int32_t x, int32_t y, uint32_t width, uint32_t height);

/**
 * @brief Renames a window.
 *
 * @param[in] win   The window.
 * @param[in] title New title.
 * @return ZUZU_OK or an error.
 */
Err WinSetTitle(Win *win, const char *title);

/**
 * @brief Closes a window and frees its surface.
 *
 * @param[in,out] win The window; cleared afterwards.
 */
void WinClose(Win *win);

/**
 * @brief Reads input events waiting for a window.
 *
 * @param[in]  win The window.
 * @param[out] out Where to put the events.
 * @param[in]  max Room in @p out.
 * @return Events read.
 */
uint32_t WinReadInput(Win *win, InputEvent *out, uint32_t max);

/**
 * @brief Lists every window.
 *
 * @param[in]  conn Connection from WinConnect().
 * @param[out] out  The list.
 * @return ZUZU_OK or an error.
 */
Err WinList(WinConn *conn, WmList *out);

/**
 * @brief Raises and focuses any window, restoring it if minimised.
 *
 * @param[in] conn Connection from WinConnect().
 * @param[in] id   The window.
 * @return ZUZU_OK or an error.
 */
Err WinFocusId(WinConn *conn, uint32_t id);

/**
 * @brief Minimises any window.
 *
 * @param[in] conn Connection from WinConnect().
 * @param[in] id   The window.
 * @return ZUZU_OK or an error.
 */
Err WinMinimizeId(WinConn *conn, uint32_t id);

#ifdef __cplusplus
}
#endif

#endif /* ZGFX_WIN_H */

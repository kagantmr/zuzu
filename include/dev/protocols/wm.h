/**
 * @file
 * @brief Window manager protocol served by wm ("/svc/wm").
 *
 * A client connects once with WM_CMD_CONNECT, granting a liveness port, and gets a token. Each
 * window is backed by a surface the client owns: a memory object holding width * height RGB565
 * pixels, granted with WM_CMD_CREATE. The client draws into it and reports changed rectangles
 * with WM_CMD_DAMAGE; wm copies them to the screen and signals WM_EVT_FRAME. wm draws the frame
 * around the surface (title bar, close and minimise buttons) and handles moving and focus.
 *
 * Input for a window arrives in its InputRing (dev/protocols/input.h), granted by
 * WM_CMD_INPUT_OPEN. Pointer coordinates are relative to the window's top-left pixel and are
 * only sent while the pointer is over the window or a button held there is still down.
 */

#ifndef WM_PROTOCOL_H
#define WM_PROTOCOL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <zuzu/err.h>
#include <zuzu/msg.h>

/** @brief Register as a client. Grant: a liveness port (PERM_WAIT). Reply: WmReply.token. */
#define WM_CMD_CONNECT 1u
/** @brief Open a window. Request: WmCreate. Grant: the surface (PERM_MAP). Reply: WmReply.id. */
#define WM_CMD_CREATE 2u
/** @brief Signal this event for the window. Request {cmd, token, id, arg = bit shift}. */
#define WM_CMD_SET_EVENT 3u
/** @brief Ask for the window's InputRing. Request {cmd, token, id}; replies with the ring granted. */
#define WM_CMD_INPUT_OPEN 4u
/** @brief Show changed pixels. Request: WmDamage. */
#define WM_CMD_DAMAGE 5u
/** @brief Close a window. Request {cmd, token, id}. */
#define WM_CMD_DESTROY 6u
/** @brief Rename a window. Request: WmCreate with only @c title used. */
#define WM_CMD_SET_TITLE 7u
/** @brief List the windows. Request {cmd, token}. Reply: WmList. */
#define WM_CMD_LIST 8u
/** @brief Raise and focus a window, restoring it if minimised. Request {cmd, token, id}. */
#define WM_CMD_FOCUS 9u
/** @brief Minimise a window. Request {cmd, token, id}. */
#define WM_CMD_MINIMIZE 10u

/** @brief Input waits in the window's InputRing. */
#define WM_EVT_INPUT (1u << 0)
/** @brief The last damage reached the screen; the client may draw again. */
#define WM_EVT_FRAME (1u << 1)
/** @brief The user asked to close the window. */
#define WM_EVT_CLOSE (1u << 2)
/** @brief The window list changed (focus, title, a window opened or closed). */
#define WM_EVT_WINDOWS (1u << 3)
/** @brief Every event bit wm may signal for one window. */
#define WM_EVT_ALL (WM_EVT_INPUT | WM_EVT_FRAME | WM_EVT_CLOSE | WM_EVT_WINDOWS)
/** @brief Bits each window needs in the client's event word. */
#define WM_EVT_BITS 4u

/** @brief Below every normal window, never focused, no frame (the desktop). */
#define WM_LAYER_DESKTOP 0u
/** @brief An ordinary framed window. */
#define WM_LAYER_NORMAL 1u
/** @brief Above every normal window, no frame (the taskbar). */
#define WM_LAYER_PANEL 2u

/** @brief Most windows wm keeps at once. */
#define WM_MAX_WINDOWS 16u
/** @brief Longest window title, including the terminating NUL. */
#define WM_TITLE_MAX 32u
/** @brief Longest title in a WM_CMD_LIST entry, including the terminating NUL. */
#define WM_LIST_TITLE_MAX 20u
/** @brief Height of the title bar wm draws above a normal window. */
#define WM_TITLE_H 28
/** @brief Width of the border wm draws around a normal window. */
#define WM_BORDER 1

/** @brief A request that names only a window. */
typedef struct {
    uint32_t cmd;   /**< A WM_CMD_* value. */
    uint32_t token; /**< From WM_CMD_CONNECT; 0 for WM_CMD_CONNECT itself. */
    uint32_t id;    /**< The window, where the command takes one. */
    uint32_t arg;   /**< Command argument: the bit shift for WM_CMD_SET_EVENT. */
} WmRequest;

/** @brief WM_CMD_CREATE and WM_CMD_SET_TITLE request. */
typedef struct {
    uint32_t cmd;               /**< WM_CMD_CREATE or WM_CMD_SET_TITLE. */
    uint32_t token;             /**< From WM_CMD_CONNECT. */
    uint32_t id;                /**< WM_CMD_SET_TITLE: the window. */
    uint32_t layer;             /**< A WM_LAYER_* value. */
    int32_t x;                  /**< Desktop and panel layers: left edge on screen. */
    int32_t y;                  /**< Desktop and panel layers: top edge on screen. */
    uint32_t width;             /**< Surface width in pixels. */
    uint32_t height;            /**< Surface height in pixels. */
    char title[WM_TITLE_MAX];   /**< NUL-terminated title. */
} WmCreate;

/** @brief WM_CMD_DAMAGE request. */
typedef struct {
    uint32_t cmd;    /**< WM_CMD_DAMAGE. */
    uint32_t token;  /**< From WM_CMD_CONNECT. */
    uint32_t id;     /**< The window. */
    int32_t x;       /**< Left edge of the changed area, in surface pixels. */
    int32_t y;       /**< Top edge of the changed area. */
    uint32_t width;  /**< Width of the changed area. */
    uint32_t height; /**< Height of the changed area. */
} WmDamage;

/** @brief Reply to every command but WM_CMD_LIST. */
typedef struct {
    Err status;      /**< ZUZU_OK or an error. */
    uint32_t token;  /**< WM_CMD_CONNECT: the client's token. */
    uint32_t id;     /**< WM_CMD_CREATE: the new window. */
    uint32_t screen_w; /**< WM_CMD_CONNECT: screen width in pixels. */
    uint32_t screen_h; /**< WM_CMD_CONNECT: screen height in pixels. */
} WmReply;

/** @brief One window in a WM_CMD_LIST reply. */
typedef struct {
    uint32_t id;                /**< The window. */
    uint8_t focused;            /**< Non-zero for the focused window. */
    uint8_t minimized;          /**< Non-zero while minimised. */
    uint8_t layer;              /**< A WM_LAYER_* value. */
    uint8_t reserved;           /**< Zero. */
    char title[WM_LIST_TITLE_MAX]; /**< NUL-terminated title, cut short if it does not fit. */
} WmWindowInfo;

/** @brief WM_CMD_LIST reply. */
typedef struct {
    Err status;                            /**< ZUZU_OK or an error. */
    uint32_t count;                        /**< Entries in @c win. */
    WmWindowInfo win[WM_MAX_WINDOWS];      /**< Windows, bottom of the stack first. */
} WmList;

_Static_assert(sizeof(WmList) <= MSG_BUF_SIZE, "WmList must fit the message buffer");
_Static_assert(sizeof(WmCreate) <= MSG_BUF_SIZE, "WmCreate must fit the message buffer");

#ifdef __cplusplus
}
#endif

#endif /* WM_PROTOCOL_H */

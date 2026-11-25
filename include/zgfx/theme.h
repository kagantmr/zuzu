/**
 * @file
 * @brief GUI themes: the colours and metrics wm and LVGL programs draw with.
 *
 * wm owns the current theme; programs read it with WinGetTheme() and are told about a change by
 * WM_EVT_THEME. Colours are 0xRRGGBB.
 */

#ifndef ZGFX_THEME_H
#define ZGFX_THEME_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/** @brief Longest theme name, including the NUL. */
#define THEME_NAME_MAX 16u

/** @brief Flat frames: a coloured border and a solid title bar. */
#define THEME_FRAME_FLAT 0u
/** @brief Raised 3D frames with a horizontal title gradient, as in 1990s desktops. */
#define THEME_FRAME_BEVEL 1u
/** @brief Frames filled with a vertical gradient, title bar included. */
#define THEME_FRAME_GRADIENT 2u

/** @brief One theme. */
typedef struct {
    char name[THEME_NAME_MAX]; /**< Name, as `theme <name>` takes it. */
    uint32_t frame;            /**< THEME_FRAME_* style of window frames and buttons. */
    uint32_t dark;             /**< Nonzero for light text on dark backgrounds. */
    uint32_t radius;           /**< Corner radius of buttons and panels, in pixels. */
    uint32_t border;           /**< Width of a window's frame, in pixels. */
    uint32_t title_h;          /**< Height of a window's title bar, in pixels. */
    uint32_t desktop_top;      /**< Wallpaper colour at the top of the screen. */
    uint32_t desktop_bottom;   /**< Wallpaper colour at the bottom of the screen. */
    uint32_t face;             /**< Window background, buttons and frames. */
    uint32_t surface;          /**< Panels and groups inside a window. */
    uint32_t field;            /**< Text fields, lists and charts. */
    uint32_t text;             /**< Normal text. */
    uint32_t text_dim;         /**< Secondary text: captions and labels. */
    uint32_t text_on_accent;   /**< Text drawn on the accent colour. */
    uint32_t accent;           /**< Selection, focus and highlights. */
    uint32_t good;             /**< Success and "up" states. */
    uint32_t bad;              /**< Errors, "down" states and the close button. */
    uint32_t title;            /**< Focused title bar, start of its gradient. */
    uint32_t title_end;        /**< Focused title bar, end of its gradient. */
    uint32_t title_off;        /**< Unfocused title bar, start of its gradient. */
    uint32_t title_off_end;    /**< Unfocused title bar, end of its gradient. */
    uint32_t title_text;       /**< Focused title text. */
    uint32_t title_text_off;   /**< Unfocused title text. */
    uint32_t light;            /**< Lit edge of a bevel. */
    uint32_t shadow;           /**< Shaded edge of a bevel, and flat borders. */
    uint32_t edge;             /**< Outermost dark edge of a bevel or frame. */
    uint32_t taskbar;          /**< Panel background. */
    uint32_t taskbar_text;     /**< Panel text. */
} Theme;

/** @brief Number of built-in themes. @return The count. */
uint32_t ThemeCount(void);

/**
 * @brief A built-in theme by position.
 *
 * @param[in] index 0 to ThemeCount() - 1; 0 is the default.
 * @return The theme, or NULL past the end.
 */
const Theme *ThemeAt(uint32_t index);

/**
 * @brief A built-in theme by name.
 *
 * @param[in] name Theme name.
 * @return The theme, or NULL if there is none by that name.
 */
const Theme *ThemeFind(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* ZGFX_THEME_H */

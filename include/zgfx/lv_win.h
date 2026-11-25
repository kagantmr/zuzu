/**
 * @file
 * @brief Runs an LVGL program in a wm window (built into liblvgl, lib/lvgl/lv_win.c).
 *
 * LvWinOpen() connects to wm, opens one window, starts LVGL with a display that renders straight
 * into the window's surface, and adds a pointer and a keypad (the keypad feeds the default group).
 * LvWinRun() runs LVGL until the user closes the window. Event bits 0 to WM_EVT_BITS - 1 belong
 * to the window; a program may wait on bits from LVWIN_FIRST_USER_BIT up with LvWinOnWake().
 */

#ifndef ZGFX_LV_WIN_H
#define ZGFX_LV_WIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <types.h>
#include <zgfx/win.h>

/** @brief First event bit a program may use for its own wake-ups. */
#define LVWIN_FIRST_USER_BIT 8u

/** @brief Called from LvWinRun() when an event bit fires. */
typedef void (*LvWinWake)(void);

/**
 * @brief Connects to wm, opens a window and starts LVGL on it.
 *
 * A width of 0 means the screen's width; a negative y counts up from the bottom of the screen.
 *
 * @param[in] spec Size, layer, position and title of the window.
 * @return ZUZU_OK or an error.
 */
Err LvWinOpen(const WinSpec *spec);

/**
 * @brief Calls @p fn from LvWinRun() whenever event bit @p bit fires.
 *
 * @param[in] bit Event bit, LVWIN_FIRST_USER_BIT to 31.
 * @param[in] fn  Function to call, or NULL to stop.
 */
void LvWinOnWake(uint32_t bit, LvWinWake fn);

/**
 * @brief Calls @p fn whenever wm reports the window list changed.
 *
 * @param[in] fn Function to call, or NULL to stop.
 */
void LvWinOnWindows(LvWinWake fn);

/**
 * @brief Runs LVGL until the user closes the window, then closes it.
 */
void LvWinRun(void);

/** @brief The event the window and LvWinOnWake() bits are signalled on. @return The event. */
Handle LvWinEvent(void);

/** @brief The connection to wm. @return The connection. */
WinConn *LvWinConn(void);

/** @brief The open window. @return The window. */
Win *LvWinWindow(void);

#ifdef __cplusplus
}
#endif

#endif /* ZGFX_LV_WIN_H */

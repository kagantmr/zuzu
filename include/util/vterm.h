/**
 * @file
 * @brief A character-cell terminal: keeps the screen contents and interprets the ANSI subset
 * zuzu's programs use (cursor movement, erase, SGR colours). Drawing goes through callbacks, so
 * the same terminal can sit on a framebuffer or inside a GUI widget.
 */

#ifndef UTIL_VTERM_H
#define UTIL_VTERM_H

#include <stdbool.h>
#include <stdint.h>
#include <util/ansi.h>

/** @brief Most columns a VTerm holds. */
#define VTERM_MAX_COLS 128
/** @brief Most rows a VTerm holds. */
#define VTERM_MAX_ROWS 64

/** @brief How a VTerm draws. Colours are RGB565. */
typedef struct {
    void (*draw_cell)(void *ctx, uint32_t col, uint32_t row, char ch, uint16_t fg,
                      uint16_t bg);                              /**< Draw one cell. */
    void (*clear_row)(void *ctx, uint32_t row, uint16_t color); /**< Fill a whole row. */
    void (*scroll_up)(void *ctx, uint16_t color); /**< Move every row up one; fill the last. */
} VTermOps;

/** @brief Where a pen colour comes from. */
typedef enum {
    VTERM_PEN_DEFAULT, /**< The terminal's default colour. */
    VTERM_PEN_INDEX,   /**< An entry of the 256-colour palette. */
    VTERM_PEN_RGB      /**< A direct RGB565 colour. */
} VTermPenKind;

/** @brief A foreground or background pen. */
typedef struct {
    VTermPenKind kind; /**< Source of the colour. */
    uint8_t index;     /**< Palette index, for VTERM_PEN_INDEX. */
    uint16_t rgb;      /**< Colour, for VTERM_PEN_RGB. */
} VTermPen;

/** @brief One terminal. */
typedef struct {
    const VTermOps *ops;                           /**< Drawing callbacks. */
    void *ctx;                                     /**< Passed to every callback. */
    uint32_t cols;                                 /**< Columns in use. */
    uint32_t rows;                                 /**< Rows in use. */
    uint32_t cx;                                   /**< Cursor column. */
    uint32_t cy;                                   /**< Cursor row. */
    uint16_t default_fg;                           /**< Default foreground. */
    uint16_t default_bg;                           /**< Default background. */
    char cells[VTERM_MAX_ROWS][VTERM_MAX_COLS];    /**< Characters on screen. */
    uint16_t fg[VTERM_MAX_ROWS][VTERM_MAX_COLS];   /**< Foreground of each cell. */
    uint16_t bg[VTERM_MAX_ROWS][VTERM_MAX_COLS];   /**< Background of each cell. */
    VTermPen pen_fg;                               /**< Current foreground pen. */
    VTermPen pen_bg;                               /**< Current background pen. */
    bool bold;                                     /**< SGR 1 is on. */
    bool reverse;                                  /**< SGR 7 is on. */
    bool wrap_pending;                             /**< Last column written; wrap on next print. */
    Ansi ansi;                                     /**< Escape-sequence parser. */
} VTerm;

/**
 * @brief Sets up a terminal and clears it to @p bg.
 *
 * @param[out] t    The terminal.
 * @param[in]  ops  Drawing callbacks; must outlive the terminal.
 * @param[in]  ctx  Passed to every callback.
 * @param[in]  cols Columns, at most VTERM_MAX_COLS.
 * @param[in]  rows Rows, at most VTERM_MAX_ROWS.
 * @param[in]  fg   Default foreground.
 * @param[in]  bg   Default background.
 */
void VTermInit(VTerm *t, const VTermOps *ops, void *ctx, uint32_t cols, uint32_t rows, uint16_t fg,
               uint16_t bg);

/**
 * @brief Feeds one byte of program output.
 *
 * @param[in,out] t The terminal.
 * @param[in]     b The byte.
 */
void VTermFeed(VTerm *t, uint8_t b);

/**
 * @brief Draws or removes the block cursor.
 *
 * @param[in,out] t  The terminal.
 * @param[in]     on true to draw it in reverse video, false to draw the cell normally.
 */
void VTermShowCursor(VTerm *t, bool on);

/**
 * @brief Draws every cell again, then the cursor if asked.
 *
 * @param[in,out] t      The terminal.
 * @param[in]     cursor true to draw the cursor too.
 */
void VTermRepaint(VTerm *t, bool cursor);

/**
 * @brief Packs an RGB colour into RGB565.
 *
 * @param[in] r Red, 0 to 255.
 * @param[in] g Green, 0 to 255.
 * @param[in] b Blue, 0 to 255.
 * @return The RGB565 value.
 */
uint16_t VTermRgb(uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief Looks up an entry of the xterm 256-colour palette.
 *
 * @param[in] index Palette index.
 * @return The RGB565 value.
 */
uint16_t VTermPaletteColor(uint8_t index);

#endif /* UTIL_VTERM_H */

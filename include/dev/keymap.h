/**
 * @file
 * @brief Turns key events (HID usage plus pressed or released) into the bytes a terminal expects.
 *
 * The layout is a compiled-in US keyboard. The state it keeps is per keyboard.
 */

#ifndef DEV_KEYMAP_H
#define DEV_KEYMAP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief Longest byte sequence KeymapFeed() produces for one event. */
#define KEYMAP_MAX_SEQ 8

/** @brief Modifier bit: left Ctrl. */
#define KEYMOD_LCTRL (1u << 0)
/** @brief Modifier bit: left Shift. */
#define KEYMOD_LSHIFT (1u << 1)
/** @brief Modifier bit: left Alt. */
#define KEYMOD_LALT (1u << 2)
/** @brief Modifier bit: left GUI. */
#define KEYMOD_LGUI (1u << 3)
/** @brief Modifier bit: right Ctrl. */
#define KEYMOD_RCTRL (1u << 4)
/** @brief Modifier bit: right Shift. */
#define KEYMOD_RSHIFT (1u << 5)
/** @brief Modifier bit: right Alt. */
#define KEYMOD_RALT (1u << 6)
/** @brief Modifier bit: right GUI. */
#define KEYMOD_RGUI (1u << 7)

/** @brief What a keyboard remembers between events. */
typedef struct {
    uint8_t mods; /**< KEYMOD_* bits for the modifier keys currently held. */
    bool caps;    /**< Caps Lock is on. */
    bool num;     /**< Num Lock is on. */
} KeymapState;

/**
 * @brief Resets a keyboard's state: no modifiers held, Caps Lock off, Num Lock on.
 *
 * @param[out] s The state to initialise.
 */
void KeymapInit(KeymapState *s);

/**
 * @brief Handles one key event.
 *
 * Modifier keys and releases produce no bytes. Characters produce one byte, Alt adds a leading
 * ESC, and navigation and function keys produce escape sequences.
 *
 * @param[in,out] s       The keyboard's state.
 * @param[in]     usage   HID usage of the key (see hid.h).
 * @param[in]     pressed True for a press (including auto-repeat), false for a release.
 * @param[out]    out     Receives the bytes.
 * @param[in]     cap     Size of @p out; KEYMAP_MAX_SEQ is always enough.
 * @return Number of bytes written to @p out, 0 if the event produces none or @p out is too small.
 */
size_t KeymapFeed(KeymapState *s, uint8_t usage, bool pressed, uint8_t *out, size_t cap);

#endif /* DEV_KEYMAP_H */

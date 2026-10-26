/**
 * @file
 * @brief USB HID keyboard usage IDs (usage page 7), the key identifiers used above the hardware.
 *
 * Keyboard drivers translate their own scancodes to these, and keymap.h turns them into bytes.
 */

#ifndef DEV_HID_H
#define DEV_HID_H

/** @brief Keyboard usage IDs. */
enum {
    HID_KEY_NONE = 0, /**< No key. */
    HID_KEY_A = 0x04, /**< Letter A. */
    HID_KEY_B = 0x05, /**< Letter B. */
    HID_KEY_C = 0x06, /**< Letter C. */
    HID_KEY_D = 0x07, /**< Letter D. */
    HID_KEY_E = 0x08, /**< Letter E. */
    HID_KEY_F = 0x09, /**< Letter F. */
    HID_KEY_G = 0x0A, /**< Letter G. */
    HID_KEY_H = 0x0B, /**< Letter H. */
    HID_KEY_I = 0x0C, /**< Letter I. */
    HID_KEY_J = 0x0D, /**< Letter J. */
    HID_KEY_K = 0x0E, /**< Letter K. */
    HID_KEY_L = 0x0F, /**< Letter L. */
    HID_KEY_M = 0x10, /**< Letter M. */
    HID_KEY_N = 0x11, /**< Letter N. */
    HID_KEY_O = 0x12, /**< Letter O. */
    HID_KEY_P = 0x13, /**< Letter P. */
    HID_KEY_Q = 0x14, /**< Letter Q. */
    HID_KEY_R = 0x15, /**< Letter R. */
    HID_KEY_S = 0x16, /**< Letter S. */
    HID_KEY_T = 0x17, /**< Letter T. */
    HID_KEY_U = 0x18, /**< Letter U. */
    HID_KEY_V = 0x19, /**< Letter V. */
    HID_KEY_W = 0x1A, /**< Letter W. */
    HID_KEY_X = 0x1B, /**< Letter X. */
    HID_KEY_Y = 0x1C, /**< Letter Y. */
    HID_KEY_Z = 0x1D, /**< Letter Z. */
    HID_KEY_1 = 0x1E, /**< Digit 1. */
    HID_KEY_2 = 0x1F, /**< Digit 2. */
    HID_KEY_3 = 0x20, /**< Digit 3. */
    HID_KEY_4 = 0x21, /**< Digit 4. */
    HID_KEY_5 = 0x22, /**< Digit 5. */
    HID_KEY_6 = 0x23, /**< Digit 6. */
    HID_KEY_7 = 0x24, /**< Digit 7. */
    HID_KEY_8 = 0x25, /**< Digit 8. */
    HID_KEY_9 = 0x26, /**< Digit 9. */
    HID_KEY_0 = 0x27, /**< Digit 0. */
    HID_KEY_ENTER = 0x28, /**< Enter. */
    HID_KEY_ESCAPE = 0x29, /**< Escape. */
    HID_KEY_BACKSPACE = 0x2A, /**< Backspace. */
    HID_KEY_TAB = 0x2B, /**< Tab. */
    HID_KEY_SPACE = 0x2C, /**< Space. */
    HID_KEY_MINUS = 0x2D, /**< Minus. */
    HID_KEY_EQUAL = 0x2E, /**< Equal. */
    HID_KEY_LBRACKET = 0x2F, /**< Left bracket. */
    HID_KEY_RBRACKET = 0x30, /**< Right bracket. */
    HID_KEY_BACKSLASH = 0x31, /**< Backslash. */
    HID_KEY_SEMICOLON = 0x33, /**< Semicolon. */
    HID_KEY_APOSTROPHE = 0x34, /**< Apostrophe. */
    HID_KEY_GRAVE = 0x35, /**< Grave accent. */
    HID_KEY_COMMA = 0x36, /**< Comma. */
    HID_KEY_PERIOD = 0x37, /**< Period. */
    HID_KEY_SLASH = 0x38, /**< Slash. */
    HID_KEY_CAPSLOCK = 0x39, /**< Caps Lock. */
    HID_KEY_F1 = 0x3A, /**< F1 key. */
    HID_KEY_F2 = 0x3B, /**< F2 key. */
    HID_KEY_F3 = 0x3C, /**< F3 key. */
    HID_KEY_F4 = 0x3D, /**< F4 key. */
    HID_KEY_F5 = 0x3E, /**< F5 key. */
    HID_KEY_F6 = 0x3F, /**< F6 key. */
    HID_KEY_F7 = 0x40, /**< F7 key. */
    HID_KEY_F8 = 0x41, /**< F8 key. */
    HID_KEY_F9 = 0x42, /**< F9 key. */
    HID_KEY_F10 = 0x43, /**< F10 key. */
    HID_KEY_F11 = 0x44, /**< F11 key. */
    HID_KEY_F12 = 0x45, /**< F12 key. */
    HID_KEY_SCROLLLOCK = 0x47, /**< Scroll Lock. */
    HID_KEY_INSERT = 0x49, /**< Insert. */
    HID_KEY_HOME = 0x4A, /**< Home. */
    HID_KEY_PAGEUP = 0x4B, /**< Page Up. */
    HID_KEY_DELETE = 0x4C, /**< Delete. */
    HID_KEY_END = 0x4D, /**< End. */
    HID_KEY_PAGEDOWN = 0x4E, /**< Page Down. */
    HID_KEY_RIGHT = 0x4F, /**< Right. */
    HID_KEY_LEFT = 0x50, /**< Left. */
    HID_KEY_DOWN = 0x51, /**< Down. */
    HID_KEY_UP = 0x52, /**< Up. */
    HID_KEY_NUMLOCK = 0x53, /**< Num Lock. */
    HID_KEY_KP_SLASH = 0x54, /**< Keypad slash. */
    HID_KEY_KP_ASTERISK = 0x55, /**< Keypad asterisk. */
    HID_KEY_KP_MINUS = 0x56, /**< Keypad minus. */
    HID_KEY_KP_PLUS = 0x57, /**< Keypad plus. */
    HID_KEY_KP_ENTER = 0x58, /**< Keypad Enter. */
    HID_KEY_KP_1 = 0x59, /**< Keypad 1. */
    HID_KEY_KP_2 = 0x5A, /**< Keypad 2. */
    HID_KEY_KP_3 = 0x5B, /**< Keypad 3. */
    HID_KEY_KP_4 = 0x5C, /**< Keypad 4. */
    HID_KEY_KP_5 = 0x5D, /**< Keypad 5. */
    HID_KEY_KP_6 = 0x5E, /**< Keypad 6. */
    HID_KEY_KP_7 = 0x5F, /**< Keypad 7. */
    HID_KEY_KP_8 = 0x60, /**< Keypad 8. */
    HID_KEY_KP_9 = 0x61, /**< Keypad 9. */
    HID_KEY_KP_0 = 0x62, /**< Keypad 0. */
    HID_KEY_KP_PERIOD = 0x63, /**< Keypad period. */
    HID_KEY_NONUS_BACKSLASH = 0x64, /**< Non-US backslash. */
    HID_KEY_APPLICATION = 0x65, /**< Application. */
    HID_KEY_LCTRL = 0xE0, /**< Left Ctrl. */
    HID_KEY_LSHIFT = 0xE1, /**< Left Shift. */
    HID_KEY_LALT = 0xE2, /**< Left Alt. */
    HID_KEY_LGUI = 0xE3, /**< Left GUI. */
    HID_KEY_RCTRL = 0xE4, /**< Right Ctrl. */
    HID_KEY_RSHIFT = 0xE5, /**< Right Shift. */
    HID_KEY_RALT = 0xE6, /**< Right Alt. */
    HID_KEY_RGUI = 0xE7, /**< Right GUI. */
};

#endif /* DEV_HID_H */

#ifndef DEV_KEYMAP_H
#define DEV_KEYMAP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define KEYMAP_MAX_SEQ 8

#define KEYMOD_LCTRL (1u << 0)
#define KEYMOD_LSHIFT (1u << 1)
#define KEYMOD_LALT (1u << 2)
#define KEYMOD_LGUI (1u << 3)
#define KEYMOD_RCTRL (1u << 4)
#define KEYMOD_RSHIFT (1u << 5)
#define KEYMOD_RALT (1u << 6)
#define KEYMOD_RGUI (1u << 7)

typedef struct {
    uint8_t mods;
    bool caps;
    bool num;
} KeymapState;

void KeymapInit(KeymapState *s);
size_t KeymapFeed(KeymapState *s, uint8_t usage, bool pressed, uint8_t *out, size_t cap);

#endif /* DEV_KEYMAP_H */

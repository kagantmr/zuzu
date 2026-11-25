/**
 * @file
 * @brief The built-in GUI themes.
 */

#include <string.h>
#include <zgfx/theme.h>

/** @brief Built-in themes; the first is the default. */
static const Theme k_themes[] = {
    {
        .name = "classic",
        .frame = THEME_FRAME_BEVEL,
        .dark = 0,
        .radius = 0,
        .border = 4,
        .title_h = 24,
        .desktop_top = 0x0f7f7f,
        .desktop_bottom = 0x056060,
        .face = 0xc0c0c0,
        .surface = 0xc0c0c0,
        .field = 0xffffff,
        .text = 0x000000,
        .text_dim = 0x404040,
        .text_on_accent = 0xffffff,
        .accent = 0x000080,
        .good = 0x007f00,
        .bad = 0xb00000,
        .title = 0x000080,
        .title_end = 0x1084d0,
        .title_off = 0x808080,
        .title_off_end = 0xb5b5b5,
        .title_text = 0xffffff,
        .title_text_off = 0xd4d0c8,
        .light = 0xffffff,
        .shadow = 0x808080,
        .edge = 0x000000,
        .taskbar = 0xc0c0c0,
        .taskbar_text = 0x000000,
    },
    {
        .name = "midnight",
        .frame = THEME_FRAME_FLAT,
        .dark = 1,
        .radius = 8,
        .border = 1,
        .title_h = 28,
        .desktop_top = 0x1d3557,
        .desktop_bottom = 0x0b1320,
        .face = 0x15202b,
        .surface = 0x1e2a38,
        .field = 0x0d131b,
        .text = 0xe6edf3,
        .text_dim = 0x8b98a5,
        .text_on_accent = 0xffffff,
        .accent = 0x2196f3,
        .good = 0x3fb950,
        .bad = 0xf85149,
        .title = 0x1f3b5c,
        .title_end = 0x1f3b5c,
        .title_off = 0x1c2633,
        .title_off_end = 0x1c2633,
        .title_text = 0xe6edf3,
        .title_text_off = 0x8b98a5,
        .light = 0x30363d,
        .shadow = 0x30363d,
        .edge = 0x30363d,
        .taskbar = 0x0d131b,
        .taskbar_text = 0xe6edf3,
    },
    {
        .name = "aero",
        .frame = THEME_FRAME_GRADIENT,
        .dark = 0,
        .radius = 4,
        .border = 5,
        .title_h = 28,
        .desktop_top = 0x2f86c8,
        .desktop_bottom = 0x0c355c,
        .face = 0xf0f0f0,
        .surface = 0xffffff,
        .field = 0xffffff,
        .text = 0x1e1e1e,
        .text_dim = 0x5f6b77,
        .text_on_accent = 0xffffff,
        .accent = 0x3399ff,
        .good = 0x2e9b45,
        .bad = 0xd9372b,
        .title = 0xcfe3f7,
        .title_end = 0x8fb6df,
        .title_off = 0xe4ebf2,
        .title_off_end = 0xc5d1dd,
        .title_text = 0x10243a,
        .title_text_off = 0x5f6b77,
        .light = 0xffffff,
        .shadow = 0x9bb4cc,
        .edge = 0x40607f,
        .taskbar = 0x1b3654,
        .taskbar_text = 0xffffff,
    },
};

uint32_t ThemeCount(void) { return (uint32_t)(sizeof(k_themes) / sizeof(k_themes[0])); }

const Theme *ThemeAt(uint32_t index) { return index < ThemeCount() ? &k_themes[index] : NULL; }

const Theme *ThemeFind(const char *name)
{
    for (uint32_t i = 0; i < ThemeCount(); i++) {
        if (strcmp(k_themes[i].name, name) == 0)
            return &k_themes[i];
    }
    return NULL;
}

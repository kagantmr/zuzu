#ifndef UI_H
#define UI_H

#define LV_CONF_INCLUDE_SIMPLE
#include "vendor/lvgl/lvgl.h"

#include <stdbool.h>
#include <stdint.h>
#include <types.h>
#include <zgfx/lv_win.h>

#define GUI_BG 0x15202b
#define GUI_PANEL 0x1e2a38
#define GUI_TEXT 0xe6edf3
#define GUI_DIM 0x8b98a5
#define GUI_ACCENT 0x2196f3
#define GUI_GOOD 0x3fb950
#define GUI_BAD 0xf85149

static inline lv_obj_t *UiRoot(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(GUI_BG), 0);
    lv_obj_set_style_text_color(scr, lv_color_hex(GUI_TEXT), 0);
    lv_obj_set_style_pad_all(scr, 12, 0);
    lv_obj_set_scrollable(scr, false);
    return scr;
}

static inline lv_obj_t *GuiPanel(lv_obj_t *parent, const char *title)
{
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_set_height(panel, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(panel, lv_color_hex(GUI_PANEL), 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_radius(panel, 10, 0);
    lv_obj_set_style_pad_all(panel, 14, 0);
    lv_obj_set_style_pad_row(panel, 6, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollable(panel, false);
    if (title) {
        lv_obj_t *label = lv_label_create(panel);
        lv_label_set_text(label, title);
        lv_obj_set_style_text_color(label, lv_color_hex(GUI_DIM), 0);
    }
    return panel;
}

static inline lv_obj_t *GuiKeyValue(lv_obj_t *parent, const char *key)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);

    lv_obj_t *k = lv_label_create(row);
    lv_label_set_text(k, key);
    lv_obj_set_style_text_color(k, lv_color_hex(GUI_DIM), 0);
    lv_obj_align(k, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *v = lv_label_create(row);
    lv_label_set_text(v, "-");
    lv_obj_set_style_text_color(v, lv_color_hex(GUI_TEXT), 0);
    lv_obj_align(v, LV_ALIGN_RIGHT_MID, 0, 0);
    return v;
}

#endif /* UI_H */

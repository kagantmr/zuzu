#include "gui.h"

#include <zuzu/syspage.h>
#include <zuzu/uptime.h>

#define TILE_W 200
#define TILE_H 150

static const GuiApp *const k_apps[] = {&g_app_memory, &g_app_netdiag, &g_app_term};

static lv_obj_t *g_content;
static lv_obj_t *g_title;
static lv_obj_t *g_back;
static lv_obj_t *g_status;
static const GuiApp *g_current;

static void UpdateStatus(lv_timer_t *timer)
{
    (void)timer;
    const Syspage *sp = (const Syspage *)SYSPAGE;
    uint32_t secs = (uint32_t)(UptimeMs() / 1000U);
    uint32_t used_mb = (sp->mem_total_kb - sp->mem_free_kb) / 1024U;
    lv_label_set_text_fmt(g_status, "%u MB used   up %02u:%02u:%02u", (unsigned)used_mb,
                          (unsigned)(secs / 3600U), (unsigned)(secs / 60U % 60U),
                          (unsigned)(secs % 60U));
}

static void OnBack(lv_event_t *e)
{
    (void)e;
    ShellHome();
}

static void OnTile(lv_event_t *e)
{
    ShellOpen(lv_event_get_user_data(e));
}

static void CloseCurrent(void)
{
    if (g_current && g_current->close)
        g_current->close();
    g_current = NULL;
    lv_obj_clean(g_content);
}

static void BuildTile(lv_obj_t *parent, const GuiApp *app)
{
    lv_obj_t *tile = lv_button_create(parent);
    lv_obj_set_size(tile, TILE_W, TILE_H);
    lv_obj_set_style_bg_color(tile, lv_color_hex(GUI_PANEL), 0);
    lv_obj_set_style_radius(tile, 14, 0);
    lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(tile, OnTile, LV_EVENT_CLICKED, (void *)app);

    lv_obj_t *icon = lv_label_create(tile);
    lv_label_set_text(icon, app->symbol);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(icon, lv_color_hex(app->color), 0);

    lv_obj_t *name = lv_label_create(tile);
    lv_label_set_text(name, app->name);
    lv_obj_set_style_text_font(name, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(name, lv_color_hex(GUI_TEXT), 0);
    lv_obj_set_style_pad_top(name, 12, 0);
}

void ShellHome(void)
{
    CloseCurrent();
    lv_label_set_text(g_title, "zuzuOS");
    lv_obj_set_hidden(g_back, true);

    lv_obj_t *grid = lv_obj_create(g_content);
    lv_obj_remove_style_all(grid);
    lv_obj_set_size(grid, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(grid, 32, 0);
    for (size_t i = 0; i < sizeof(k_apps) / sizeof(k_apps[0]); i++)
        BuildTile(grid, k_apps[i]);
}

void ShellOpen(const GuiApp *app)
{
    CloseCurrent();
    g_current = app;
    lv_label_set_text(g_title, app->name);
    lv_obj_set_hidden(g_back, false);
    app->open(g_content);
}

lv_obj_t *GuiPanel(lv_obj_t *parent, const char *title)
{
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_set_style_bg_color(panel, lv_color_hex(GUI_PANEL), 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_radius(panel, 12, 0);
    lv_obj_set_style_pad_all(panel, 16, 0);
    lv_obj_set_style_pad_row(panel, 8, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollable(panel, false);
    if (title) {
        lv_obj_t *label = lv_label_create(panel);
        lv_label_set_text(label, title);
        lv_obj_set_style_text_color(label, lv_color_hex(GUI_DIM), 0);
    }
    return panel;
}

lv_obj_t *GuiKeyValue(lv_obj_t *parent, const char *key)
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

void ShellInit(void)
{
    lv_display_t *disp = lv_display_get_default();
    lv_display_set_theme(disp, lv_theme_default_init(disp, lv_color_hex(GUI_ACCENT),
                                                     lv_color_hex(GUI_BAD), true,
                                                     LV_FONT_DEFAULT));
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(GUI_BG), 0);
    lv_obj_set_style_text_color(scr, lv_color_hex(GUI_TEXT), 0);
    lv_obj_set_scrollable(scr, false);

    lv_obj_t *bar = lv_obj_create(scr);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, LV_PCT(100), GUI_BAR_H);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(GUI_PANEL), 0);
    lv_obj_set_style_pad_hor(bar, 12, 0);

    g_back = lv_button_create(bar);
    lv_obj_set_size(g_back, 96, 32);
    lv_obj_align(g_back, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_add_event_cb(g_back, OnBack, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back_label = lv_label_create(g_back);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT " Home");
    lv_obj_center(back_label);

    g_title = lv_label_create(bar);
    lv_obj_set_style_text_font(g_title, &lv_font_montserrat_20, 0);
    lv_obj_align(g_title, LV_ALIGN_CENTER, 0, 0);

    g_status = lv_label_create(bar);
    lv_obj_set_style_text_color(g_status, lv_color_hex(GUI_DIM), 0);
    lv_obj_align(g_status, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_timer_create(UpdateStatus, 1000, NULL);
    UpdateStatus(NULL);

    g_content = lv_obj_create(scr);
    lv_obj_remove_style_all(g_content);
    lv_obj_set_size(g_content, LV_PCT(100), lv_display_get_vertical_resolution(NULL) - GUI_BAR_H);
    lv_obj_set_pos(g_content, 0, GUI_BAR_H);
    lv_obj_set_style_pad_all(g_content, 16, 0);
    lv_obj_set_scrollable(g_content, false);

    ShellHome();
}

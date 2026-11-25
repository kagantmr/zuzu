#include "gui.h"

#include <zuzu/syspage.h>
#include <zuzu/uptime.h>

#define APP_COUNT (sizeof(k_apps) / sizeof(k_apps[0]))
#define ICON_SIZE 56
#define ICON_CELL_W 96
#define ICON_CELL_H 92
#define CASCADE 32
#define WIN_BORDER 0x30363d
#define TITLE_IDLE 0x1c2633
#define TITLE_FOCUS 0x1f3b5c
#define TASK_W 150
#define MENU_W 240

static const GuiApp *const k_apps[] = {&g_app_term, &g_app_netdiag, &g_app_memory};

typedef struct {
    lv_obj_t *frame;
    lv_obj_t *title;
    lv_obj_t *task;
    bool minimized;
} Window;

static Window g_wins[APP_COUNT];
static int32_t g_focus = -1;
static uint32_t g_opened;
static lv_obj_t *g_taskbar;
static lv_obj_t *g_tasks;
static lv_obj_t *g_menu;
static lv_obj_t *g_tray;

static int32_t AppIndex(const GuiApp *app)
{
    for (uint32_t i = 0; i < APP_COUNT; i++) {
        if (k_apps[i] == app)
            return (int32_t)i;
    }
    return -1;
}

static int32_t WindowOf(lv_obj_t *obj)
{
    for (; obj; obj = lv_obj_get_parent(obj)) {
        for (uint32_t i = 0; i < APP_COUNT; i++) {
            if (g_wins[i].frame == obj)
                return (int32_t)i;
        }
    }
    return -1;
}

static void KeepChromeOnTop(void)
{
    lv_obj_move_foreground(g_taskbar);
    lv_obj_move_foreground(g_menu);
}

static void PaintFocus(void)
{
    for (uint32_t i = 0; i < APP_COUNT; i++) {
        Window *w = &g_wins[i];
        if (!w->frame)
            continue;
        bool focused = (int32_t)i == g_focus;
        lv_obj_set_style_bg_color(w->title, lv_color_hex(focused ? TITLE_FOCUS : TITLE_IDLE), 0);
        lv_obj_set_style_border_color(w->frame, lv_color_hex(focused ? GUI_ACCENT : WIN_BORDER),
                                      0);
        lv_obj_set_state(w->task, LV_STATE_CHECKED, focused);
    }
}

static void FocusTopmost(void)
{
    g_focus = -1;
    lv_obj_t *scr = lv_screen_active();
    for (int32_t c = (int32_t)lv_obj_get_child_count(scr) - 1; c >= 0; c--) {
        lv_obj_t *child = lv_obj_get_child(scr, c);
        int32_t i = WindowOf(child);
        if (i >= 0 && g_wins[i].frame == child && !g_wins[i].minimized) {
            g_focus = i;
            break;
        }
    }
    PaintFocus();
}

static void Focus(int32_t i)
{
    Window *w = &g_wins[i];
    if (w->minimized) {
        w->minimized = false;
        lv_obj_set_hidden(w->frame, false);
    }
    lv_obj_move_foreground(w->frame);
    KeepChromeOnTop();
    g_focus = i;
    PaintFocus();
}

static void Minimize(int32_t i)
{
    g_wins[i].minimized = true;
    lv_obj_set_hidden(g_wins[i].frame, true);
    FocusTopmost();
}

static void CloseWindow(int32_t i)
{
    Window *w = &g_wins[i];
    if (k_apps[i]->close)
        k_apps[i]->close();
    lv_obj_delete(w->frame);
    lv_obj_delete(w->task);
    *w = (Window){0};
    FocusTopmost();
}

static bool RouteKey(uint8_t usage, bool pressed)
{
    if (g_focus < 0 || !k_apps[g_focus]->key)
        return false;
    return k_apps[g_focus]->key(usage, pressed);
}

static bool Inside(lv_obj_t *obj, lv_obj_t *ancestor)
{
    for (; obj; obj = lv_obj_get_parent(obj)) {
        if (obj == ancestor)
            return true;
    }
    return false;
}

static void OnPointerPressed(lv_event_t *e)
{
    (void)e;
    lv_obj_t *obj = lv_indev_get_active_obj();
    int32_t i = WindowOf(obj);
    if (i >= 0 && i != g_focus)
        Focus(i);
    if (!Inside(obj, g_menu) && !Inside(obj, g_taskbar))
        lv_obj_set_hidden(g_menu, true);
}

static void OnTitleDrag(lv_event_t *e)
{
    lv_obj_t *frame = lv_event_get_user_data(e);
    lv_point_t v;
    lv_indev_get_vect(lv_indev_active(), &v);
    int32_t max_x = lv_display_get_horizontal_resolution(NULL) - 80;
    int32_t max_y = lv_display_get_vertical_resolution(NULL) - GUI_TASKBAR_H - GUI_TITLE_H;
    int32_t x = LV_CLAMP(80 - lv_obj_get_width(frame), lv_obj_get_x(frame) + v.x, max_x);
    int32_t y = LV_CLAMP(0, lv_obj_get_y(frame) + v.y, max_y);
    lv_obj_set_pos(frame, x, y);
}

static void OnClose(lv_event_t *e) { CloseWindow((int32_t)(intptr_t)lv_event_get_user_data(e)); }

static void OnMinimize(lv_event_t *e) { Minimize((int32_t)(intptr_t)lv_event_get_user_data(e)); }

static void OnTask(lv_event_t *e)
{
    int32_t i = (int32_t)(intptr_t)lv_event_get_user_data(e);
    if (!g_wins[i].minimized && i == g_focus)
        Minimize(i);
    else
        Focus(i);
}

static lv_obj_t *TitleButton(lv_obj_t *bar, const char *symbol, uint32_t hover, lv_event_cb_t cb,
                             int32_t i)
{
    lv_obj_t *btn = lv_button_create(bar);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, 30, 24);
    lv_obj_set_style_radius(btn, 6, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(hover), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, symbol);
    lv_obj_set_style_text_color(label, lv_color_hex(GUI_TEXT), 0);
    lv_obj_center(label);
    return btn;
}

static lv_obj_t *BuildFrame(int32_t i)
{
    const GuiApp *app = k_apps[i];
    lv_obj_t *scr = lv_screen_active();
    int32_t sw = lv_display_get_horizontal_resolution(NULL);
    int32_t sh = lv_display_get_vertical_resolution(NULL) - GUI_TASKBAR_H;
    int32_t w = LV_MIN(app->width, sw);
    int32_t h = LV_MIN(app->height + GUI_TITLE_H, sh);

    lv_obj_t *frame = lv_obj_create(scr);
    lv_obj_remove_style_all(frame);
    lv_obj_set_size(frame, w, h);
    int32_t step = (int32_t)(g_opened++ % 6U) * CASCADE;
    lv_obj_set_pos(frame, LV_MIN(40 + step, sw - w), LV_MIN(24 + step, sh - h));
    lv_obj_set_style_bg_opa(frame, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(frame, lv_color_hex(GUI_BG), 0);
    lv_obj_set_style_radius(frame, 10, 0);
    lv_obj_set_style_clip_corner(frame, true, 0);
    lv_obj_set_style_border_width(frame, 1, 0);
    lv_obj_set_style_border_color(frame, lv_color_hex(WIN_BORDER), 0);
    lv_obj_set_style_shadow_width(frame, 28, 0);
    lv_obj_set_style_shadow_opa(frame, LV_OPA_50, 0);
    lv_obj_set_style_shadow_offset_y(frame, 6, 0);
    lv_obj_set_scrollable(frame, false);

    lv_obj_t *title = lv_obj_create(frame);
    lv_obj_remove_style_all(title);
    lv_obj_set_size(title, LV_PCT(100), GUI_TITLE_H);
    lv_obj_set_style_bg_opa(title, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(title, 10, 0);
    lv_obj_set_flex_flow(title, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(title, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(title, 8, 0);
    lv_obj_add_event_cb(title, OnTitleDrag, LV_EVENT_PRESSING, frame);
    g_wins[i].title = title;

    lv_obj_t *icon = lv_label_create(title);
    lv_label_set_text(icon, app->symbol);
    lv_obj_set_style_text_color(icon, lv_color_hex(app->color), 0);
    lv_obj_t *name = lv_label_create(title);
    lv_label_set_text(name, app->name);
    lv_obj_set_flex_grow(name, 1);
    TitleButton(title, LV_SYMBOL_MINUS, GUI_PANEL, OnMinimize, i);
    TitleButton(title, LV_SYMBOL_CLOSE, GUI_BAD, OnClose, i);

    lv_obj_t *body = lv_obj_create(frame);
    lv_obj_remove_style_all(body);
    lv_obj_set_pos(body, 0, GUI_TITLE_H);
    lv_obj_set_size(body, w, h - GUI_TITLE_H);
    lv_obj_set_style_pad_all(body, 12, 0);
    lv_obj_set_scrollable(body, false);
    return body;
}

static lv_obj_t *BuildTask(int32_t i)
{
    const GuiApp *app = k_apps[i];
    lv_obj_t *btn = lv_button_create(g_tasks);
    lv_obj_set_size(btn, TASK_W, GUI_TASKBAR_H - 10);
    lv_obj_set_style_bg_color(btn, lv_color_hex(GUI_PANEL), 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(TITLE_FOCUS), LV_STATE_CHECKED);
    lv_obj_set_style_border_side(btn, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_border_width(btn, 2, LV_STATE_CHECKED);
    lv_obj_set_style_border_color(btn, lv_color_hex(GUI_ACCENT), LV_STATE_CHECKED);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_radius(btn, 6, 0);
    lv_obj_add_event_cb(btn, OnTask, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text_fmt(label, "%s  %s", app->symbol, app->name);
    lv_obj_set_style_text_color(label, lv_color_hex(GUI_TEXT), 0);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);
    return btn;
}

void DesktopLaunch(const GuiApp *app)
{
    lv_obj_set_hidden(g_menu, true);
    int32_t i = AppIndex(app);
    if (i < 0)
        return;
    if (g_wins[i].frame) {
        Focus(i);
        return;
    }
    lv_obj_t *body = BuildFrame(i);
    g_wins[i].frame = lv_obj_get_parent(body);
    g_wins[i].task = BuildTask(i);
    app->open(body);
    Focus(i);
}

static void OnLaunch(lv_event_t *e) { DesktopLaunch(lv_event_get_user_data(e)); }

static void OnStart(lv_event_t *e)
{
    (void)e;
    bool hidden = lv_obj_is_hidden(g_menu);
    lv_obj_set_hidden(g_menu, !hidden);
    if (hidden)
        lv_obj_move_foreground(g_menu);
}

static void OnConsole(lv_event_t *e)
{
    (void)e;
    lv_obj_set_hidden(g_menu, true);
    GuiSwitchVt(1);
}

static void UpdateTray(lv_timer_t *timer)
{
    (void)timer;
    const Syspage *sp = (const Syspage *)SYSPAGE;
    uint32_t secs = (uint32_t)(UptimeMs() / 1000U);
    uint32_t used_mb = (sp->mem_total_kb - sp->mem_free_kb) / 1024U;
    lv_label_set_text_fmt(g_tray, LV_SYMBOL_SD_CARD " %u MB     %02u:%02u:%02u", (unsigned)used_mb,
                          (unsigned)(secs / 3600U), (unsigned)(secs / 60U % 60U),
                          (unsigned)(secs % 60U));
}

static void BuildIcon(lv_obj_t *parent, const GuiApp *app)
{
    lv_obj_t *cell = lv_button_create(parent);
    lv_obj_remove_style_all(cell);
    lv_obj_set_size(cell, ICON_CELL_W, ICON_CELL_H);
    lv_obj_set_style_radius(cell, 8, 0);
    lv_obj_set_style_bg_color(cell, lv_color_white(), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(cell, LV_OPA_20, LV_STATE_PRESSED);
    lv_obj_set_flex_flow(cell, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cell, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(cell, 6, 0);
    lv_obj_add_event_cb(cell, OnLaunch, LV_EVENT_CLICKED, (void *)app);

    lv_obj_t *tile = lv_obj_create(cell);
    lv_obj_remove_style_all(tile);
    lv_obj_set_size(tile, ICON_SIZE, ICON_SIZE);
    lv_obj_set_style_radius(tile, 14, 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(tile, lv_color_hex(app->color), 0);
    lv_obj_set_style_bg_grad_color(tile, lv_color_darken(lv_color_hex(app->color), LV_OPA_40), 0);
    lv_obj_set_style_bg_grad_dir(tile, LV_GRAD_DIR_VER, 0);
    lv_obj_set_clickable(tile, false);
    lv_obj_t *sym = lv_label_create(tile);
    lv_label_set_text(sym, app->symbol);
    lv_obj_set_style_text_font(sym, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(sym, lv_color_white(), 0);
    lv_obj_center(sym);

    lv_obj_t *name = lv_label_create(cell);
    lv_label_set_text(name, app->name);
    lv_obj_set_style_text_color(name, lv_color_white(), 0);
}

static void MenuItem(const char *symbol, const char *text, lv_event_cb_t cb, void *data)
{
    lv_obj_t *btn = lv_button_create(g_menu);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, LV_PCT(100), 40);
    lv_obj_set_style_radius(btn, 6, 0);
    lv_obj_set_style_pad_hor(btn, 12, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(TITLE_FOCUS), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(btn, 12, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, data);
    lv_obj_t *icon = lv_label_create(btn);
    lv_label_set_text(icon, symbol);
    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
}

static void BuildMenu(lv_obj_t *scr)
{
    g_menu = lv_obj_create(scr);
    lv_obj_set_size(g_menu, MENU_W, LV_SIZE_CONTENT);
    lv_obj_align(g_menu, LV_ALIGN_BOTTOM_LEFT, 6, -(GUI_TASKBAR_H + 6));
    lv_obj_set_style_bg_color(g_menu, lv_color_hex(GUI_PANEL), 0);
    lv_obj_set_style_border_color(g_menu, lv_color_hex(WIN_BORDER), 0);
    lv_obj_set_style_radius(g_menu, 10, 0);
    lv_obj_set_style_pad_all(g_menu, 8, 0);
    lv_obj_set_style_pad_row(g_menu, 2, 0);
    lv_obj_set_style_shadow_width(g_menu, 24, 0);
    lv_obj_set_style_shadow_opa(g_menu, LV_OPA_50, 0);
    lv_obj_set_flex_flow(g_menu, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollable(g_menu, false);

    lv_obj_t *head = lv_label_create(g_menu);
    lv_label_set_text(head, "zuzuOS");
    lv_obj_set_style_text_color(head, lv_color_hex(GUI_DIM), 0);
    lv_obj_set_style_pad_all(head, 8, 0);
    for (uint32_t i = 0; i < APP_COUNT; i++)
        MenuItem(k_apps[i]->symbol, k_apps[i]->name, OnLaunch, (void *)k_apps[i]);
    MenuItem(LV_SYMBOL_NEW_LINE, "Text console (F1)", OnConsole, NULL);
    lv_obj_set_hidden(g_menu, true);
}

static void BuildTaskbar(lv_obj_t *scr)
{
    g_taskbar = lv_obj_create(scr);
    lv_obj_remove_style_all(g_taskbar);
    lv_obj_set_size(g_taskbar, LV_PCT(100), GUI_TASKBAR_H);
    lv_obj_align(g_taskbar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(g_taskbar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(g_taskbar, lv_color_hex(0x0d131b), 0);
    lv_obj_set_style_border_side(g_taskbar, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_width(g_taskbar, 1, 0);
    lv_obj_set_style_border_color(g_taskbar, lv_color_hex(WIN_BORDER), 0);
    lv_obj_set_style_pad_hor(g_taskbar, 6, 0);
    lv_obj_set_flex_flow(g_taskbar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(g_taskbar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(g_taskbar, 8, 0);
    lv_obj_set_scrollable(g_taskbar, false);

    lv_obj_t *start = lv_button_create(g_taskbar);
    lv_obj_set_size(start, LV_SIZE_CONTENT, GUI_TASKBAR_H - 10);
    lv_obj_set_style_radius(start, 6, 0);
    lv_obj_set_style_shadow_width(start, 0, 0);
    lv_obj_add_event_cb(start, OnStart, LV_EVENT_CLICKED, NULL);
    lv_obj_t *label = lv_label_create(start);
    lv_label_set_text(label, LV_SYMBOL_LIST "  zuzu");
    lv_obj_center(label);

    g_tasks = lv_obj_create(g_taskbar);
    lv_obj_remove_style_all(g_tasks);
    lv_obj_set_height(g_tasks, LV_PCT(100));
    lv_obj_set_flex_grow(g_tasks, 1);
    lv_obj_set_flex_flow(g_tasks, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(g_tasks, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(g_tasks, 6, 0);
    lv_obj_set_scrollable(g_tasks, false);

    g_tray = lv_label_create(g_taskbar);
    lv_obj_set_style_text_color(g_tray, lv_color_hex(GUI_DIM), 0);
    lv_obj_set_style_pad_right(g_tray, 8, 0);
    lv_timer_create(UpdateTray, 1000, NULL);
    UpdateTray(NULL);
}

static void BuildDesktop(lv_obj_t *scr)
{
    lv_obj_t *desk = lv_obj_create(scr);
    lv_obj_remove_style_all(desk);
    lv_obj_set_size(desk, LV_PCT(100),
                    lv_display_get_vertical_resolution(NULL) - GUI_TASKBAR_H);
    lv_obj_set_style_bg_opa(desk, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(desk, lv_color_hex(0x1d3557), 0);
    lv_obj_set_style_bg_grad_color(desk, lv_color_hex(0x0b1320), 0);
    lv_obj_set_style_bg_grad_dir(desk, LV_GRAD_DIR_VER, 0);
    lv_obj_set_scrollable(desk, false);

    lv_obj_t *mark = lv_label_create(desk);
    lv_label_set_text(mark, "zuzuOS");
    lv_obj_set_style_text_font(mark, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(mark, lv_color_white(), 0);
    lv_obj_set_style_text_opa(mark, LV_OPA_20, 0);
    lv_obj_align(mark, LV_ALIGN_BOTTOM_RIGHT, -24, -16);

    lv_obj_t *icons = lv_obj_create(desk);
    lv_obj_remove_style_all(icons);
    lv_obj_set_size(icons, ICON_CELL_W, LV_PCT(100));
    lv_obj_set_pos(icons, 12, 12);
    lv_obj_set_flex_flow(icons, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(icons, 8, 0);
    for (uint32_t i = 0; i < APP_COUNT; i++)
        BuildIcon(icons, k_apps[i]);
}

lv_obj_t *GuiPanel(lv_obj_t *parent, const char *title)
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

static void HookPointer(void)
{
    for (lv_indev_t *in = lv_indev_get_next(NULL); in; in = lv_indev_get_next(in)) {
        if (lv_indev_get_type(in) == LV_INDEV_TYPE_POINTER)
            lv_indev_add_event_cb(in, OnPointerPressed, LV_EVENT_PRESSED, NULL);
    }
}

void DesktopInit(void)
{
    lv_display_t *disp = lv_display_get_default();
    lv_display_set_theme(disp, lv_theme_default_init(disp, lv_color_hex(GUI_ACCENT),
                                                     lv_color_hex(GUI_BAD), true,
                                                     LV_FONT_DEFAULT));
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(GUI_BG), 0);
    lv_obj_set_style_text_color(scr, lv_color_hex(GUI_TEXT), 0);
    lv_obj_set_scrollable(scr, false);

    BuildDesktop(scr);
    BuildTaskbar(scr);
    BuildMenu(scr);
    HookPointer();
    GuiSetKeyHook(RouteKey);
}

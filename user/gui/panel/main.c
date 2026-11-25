#include "ui.h"

#include <fs/fsd_client.h>
#include <fs/protocols/fsd.h>
#include <string.h>
#include <util/spawn.h>
#include <zuzu/syspage.h>
#include <zuzu/uptime.h>
#include <zuzu/zuzu.h>

#define LOG_TAG "panel"
#include <util/log.h>

#define PANEL_H 44
#define TASK_W 160
#define FIRST_CHILD_BIT 10
#define MAX_CHILDREN 16
#define TASKBAR_BG 0x0d131b
#define TASK_FOCUS 0x1f3b5c

typedef struct {
    const char *name;
    const char *symbol;
    uint32_t color;
    const char *path;
} Launcher;

typedef struct {
    Handle task;
    Spid pid;
} Child;

static const Launcher k_launchers[] = {
    {"Terminal", LV_SYMBOL_KEYBOARD, 0xbc8cff, "/bin/term"},
    {"Netdiag", LV_SYMBOL_WIFI, GUI_GOOD, "/bin/netdiag"},
    {"Memory", LV_SYMBOL_SD_CARD, 0xd29922, "/bin/memory"},
};

static Child g_children[MAX_CHILDREN];
static FsdConn g_fsd;
static bool g_fsd_up;
static lv_obj_t *g_tasks;
static lv_obj_t *g_tray;
static WmList g_list;

static void ReapChild(uint32_t slot)
{
    Child *c = &g_children[slot];
    if (c->task >= 0)
        HandleClose(c->task);
    if (c->pid >= 0)
        HandleDestroy((Handle)c->pid);
    *c = (Child){.task = -1, .pid = -1};
}

#define REAPER(n)                                                                                  \
    static void Reap##n(void) { ReapChild(n); }
REAPER(0)
REAPER(1)
REAPER(2)
REAPER(3)
REAPER(4)
REAPER(5)
REAPER(6)
REAPER(7)
REAPER(8)
REAPER(9)
REAPER(10)
REAPER(11)
REAPER(12)
REAPER(13)
REAPER(14)
REAPER(15)

static const LvWinWake k_reapers[MAX_CHILDREN] = {Reap0,  Reap1,  Reap2,  Reap3, Reap4,  Reap5,
                                                  Reap6,  Reap7,  Reap8,  Reap9, Reap10, Reap11,
                                                  Reap12, Reap13, Reap14, Reap15};

static void Launch(const char *path)
{
    uint32_t slot = MAX_CHILDREN;
    for (uint32_t i = 0; i < MAX_CHILDREN; i++) {
        if (g_children[i].task < 0) {
            slot = i;
            break;
        }
    }
    if (slot == MAX_CHILDREN)
        return;
    if (!g_fsd_up) {
        if (FsdConnect(&g_fsd, FSD_SHM_DEFAULT) != ZUZU_OK)
            return;
        g_fsd_up = true;
    }
    Handle image = -1;
    size_t size = 0;
    Err rc = FsdReadImage(&g_fsd, path, &image, &size);
    if (rc != ZUZU_OK) {
        LOG_WARN(LOG_TAG, "cannot read %s: %s", path, StrToError(rc));
        return;
    }
    static const char env[] = "HOME=/\0PWD=/";
    SpawnAttr attr = {.prio = SPAWN_PRIO_INHERIT,
                      .max_prio = SPAWN_PRIO_INHERIT,
                      .envbuf = env,
                      .envbuf_len = sizeof(env),
                      .envc = 2};
    Child *c = &g_children[slot];
    rc = SpawnProcessObjAttr(image, size, strrchr(path, '/') + 1, NULL, 0, 0, &attr, &c->pid,
                             &c->task);
    HandleClose(image);
    if (rc != ZUZU_OK) {
        LOG_WARN(LOG_TAG, "cannot start %s: %s", path, StrToError(rc));
        *c = (Child){.task = -1, .pid = -1};
        return;
    }
    Bind(EVENT_TASK, LvWinEvent(), c->task, FIRST_CHILD_BIT + slot);
}

static void OnLaunch(lv_event_t *e)
{
    const Launcher *l = lv_event_get_user_data(e);
    Launch(l->path);
}

static void OnTask(lv_event_t *e)
{
    uint32_t id = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
    for (uint32_t i = 0; i < g_list.count; i++) {
        const WmWindowInfo *w = &g_list.win[i];
        if (w->id != id)
            continue;
        if (w->focused && !w->minimized)
            WinMinimizeId(LvWinConn(), id);
        else
            WinFocusId(LvWinConn(), id);
        return;
    }
}

static void RefreshTasks(void)
{
    if (WinList(LvWinConn(), &g_list) != ZUZU_OK)
        return;
    lv_obj_clean(g_tasks);
    for (uint32_t i = 0; i < g_list.count; i++) {
        const WmWindowInfo *w = &g_list.win[i];
        if (w->layer != WM_LAYER_NORMAL)
            continue;
        lv_obj_t *btn = lv_button_create(g_tasks);
        lv_obj_set_size(btn, TASK_W, PANEL_H - 10);
        lv_obj_set_style_radius(btn, 6, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(w->focused ? TASK_FOCUS : GUI_PANEL), 0);
        lv_obj_set_style_border_side(btn, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_width(btn, w->focused ? 2 : 0, 0);
        lv_obj_set_style_border_color(btn, lv_color_hex(GUI_ACCENT), 0);
        lv_obj_add_event_cb(btn, OnTask, LV_EVENT_CLICKED, (void *)(uintptr_t)w->id);
        lv_obj_t *label = lv_label_create(btn);
        lv_label_set_text(label, w->title);
        lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_width(label, TASK_W - 20);
        lv_obj_set_style_text_color(label, lv_color_hex(w->minimized ? GUI_DIM : GUI_TEXT), 0);
        lv_obj_center(label);
    }
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

static void BuildLauncher(lv_obj_t *bar, const Launcher *l)
{
    lv_obj_t *btn = lv_button_create(bar);
    lv_obj_set_size(btn, PANEL_H - 8, PANEL_H - 8);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(l->color), 0);
    lv_obj_set_style_bg_grad_color(btn, lv_color_darken(lv_color_hex(l->color), LV_OPA_40), 0);
    lv_obj_set_style_bg_grad_dir(btn, LV_GRAD_DIR_VER, 0);
    lv_obj_add_event_cb(btn, OnLaunch, LV_EVENT_CLICKED, (void *)l);
    lv_obj_t *sym = lv_label_create(btn);
    lv_label_set_text(sym, l->symbol);
    lv_obj_set_style_text_color(sym, lv_color_white(), 0);
    lv_obj_center(sym);
}

static void Build(void)
{
    lv_obj_t *bar = lv_screen_active();
    lv_obj_set_style_bg_color(bar, lv_color_hex(TASKBAR_BG), 0);
    lv_obj_set_style_text_color(bar, lv_color_hex(GUI_TEXT), 0);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_width(bar, 1, 0);
    lv_obj_set_style_border_color(bar, lv_color_hex(0x30363d), 0);
    lv_obj_set_style_pad_hor(bar, 8, 0);
    lv_obj_set_style_pad_column(bar, 8, 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollable(bar, false);

    for (size_t i = 0; i < sizeof(k_launchers) / sizeof(k_launchers[0]); i++)
        BuildLauncher(bar, &k_launchers[i]);

    lv_obj_t *sep = lv_obj_create(bar);
    lv_obj_remove_style_all(sep);
    lv_obj_set_size(sep, 1, PANEL_H - 16);
    lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(sep, lv_color_hex(0x30363d), 0);

    g_tasks = lv_obj_create(bar);
    lv_obj_remove_style_all(g_tasks);
    lv_obj_set_height(g_tasks, LV_PCT(100));
    lv_obj_set_flex_grow(g_tasks, 1);
    lv_obj_set_flex_flow(g_tasks, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(g_tasks, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(g_tasks, 6, 0);
    lv_obj_set_scrollable(g_tasks, false);

    g_tray = lv_label_create(bar);
    lv_obj_set_style_text_color(g_tray, lv_color_hex(GUI_DIM), 0);
    lv_timer_create(UpdateTray, 1000, NULL);
    UpdateTray(NULL);
}

int main(void)
{
    for (uint32_t i = 0; i < MAX_CHILDREN; i++)
        g_children[i] = (Child){.task = -1, .pid = -1};
    WinSpec spec = {.width = 0, .height = PANEL_H, .layer = WM_LAYER_PANEL, .y = -PANEL_H,
                    .title = "panel"};
    Err rc = LvWinOpen(&spec);
    if (rc != ZUZU_OK) {
        LOG_ERROR(LOG_TAG, "cannot open the panel: %s", StrToError(rc));
        return 1;
    }
    for (uint32_t i = 0; i < MAX_CHILDREN; i++)
        LvWinOnWake(FIRST_CHILD_BIT + i, k_reapers[i]);
    LvWinOnWindows(RefreshTasks);
    Build();
    RefreshTasks();
    LvWinRun();
    return 0;
}

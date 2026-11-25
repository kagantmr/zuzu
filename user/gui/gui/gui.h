#ifndef GUI_H
#define GUI_H

#define LV_CONF_INCLUDE_SIMPLE
#include "vendor/lvgl/lvgl.h"

#include <stdbool.h>
#include <stdint.h>
#include <types.h>

#define GUI_TASKBAR_H 44
#define GUI_TITLE_H 34
#define GUI_BG 0x15202b
#define GUI_PANEL 0x1e2a38
#define GUI_TEXT 0xe6edf3
#define GUI_DIM 0x8b98a5
#define GUI_ACCENT 0x2196f3
#define GUI_GOOD 0x3fb950
#define GUI_BAD 0xf85149

#define GUI_BIT_TTY 8
#define GUI_BIT_CHILD 9

typedef bool (*GuiKeyHook)(uint8_t usage, bool pressed);
typedef void (*GuiWakeHook)(void);

typedef struct {
    const char *name;
    const char *symbol;
    uint32_t color;
    int32_t width;
    int32_t height;
    void (*open)(lv_obj_t *parent);
    void (*close)(void);
    bool (*key)(uint8_t usage, bool pressed);
} GuiApp;

extern const GuiApp g_app_memory;
extern const GuiApp g_app_netdiag;
extern const GuiApp g_app_term;

Handle GuiEvent(void);
void GuiSetKeyHook(GuiKeyHook hook);
void GuiSetWakeHook(uint32_t bit, GuiWakeHook hook);
Err GuiSwitchVt(uint32_t vt);

void DesktopInit(void);
void DesktopLaunch(const GuiApp *app);

lv_obj_t *GuiPanel(lv_obj_t *parent, const char *title);
lv_obj_t *GuiKeyValue(lv_obj_t *parent, const char *key);

#endif /* GUI_H */

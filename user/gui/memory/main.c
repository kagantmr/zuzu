#include "ui.h"

#include <zuzu/syspage.h>

#define HISTORY 60

static lv_obj_t *g_arc;
static lv_obj_t *g_pct;
static lv_obj_t *g_total;
static lv_obj_t *g_used;
static lv_obj_t *g_free;
static lv_obj_t *g_lbx;
static lv_obj_t *g_chart;
static lv_chart_series_t *g_series;

static void Sample(lv_timer_t *timer)
{
    (void)timer;
    const Syspage *sp = (const Syspage *)SYSPAGE;
    uint32_t total = sp->mem_total_kb;
    uint32_t free_kb = sp->mem_free_kb;
    uint32_t used = total - free_kb;
    uint32_t pct = total ? (uint32_t)((uint64_t)used * 100U / total) : 0;

    lv_arc_set_value(g_arc, (int32_t)pct);
    lv_label_set_text_fmt(g_pct, "%u%%", (unsigned)pct);
    lv_label_set_text_fmt(g_total, "%u KB", (unsigned)total);
    lv_label_set_text_fmt(g_used, "%u KB", (unsigned)used);
    lv_label_set_text_fmt(g_free, "%u KB", (unsigned)free_kb);
    lv_label_set_text_fmt(g_lbx, "%u", (unsigned)sp->lbx_pending);
    lv_chart_set_next_value(g_chart, g_series, (int32_t)(used / 1024U));
}

static void Build(lv_obj_t *parent)
{
    const Syspage *sp = (const Syspage *)SYSPAGE;
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 12, 0);

    lv_obj_t *top = lv_obj_create(parent);
    lv_obj_remove_style_all(top);
    lv_obj_set_size(top, LV_PCT(100), 230);
    lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(top, 12, 0);

    lv_obj_t *gauge = GuiPanel(top, "Physical memory in use");
    lv_obj_set_size(gauge, 260, LV_PCT(100));
    lv_obj_set_flex_align(gauge, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    g_arc = lv_arc_create(gauge);
    lv_obj_set_size(g_arc, 160, 160);
    lv_arc_set_range(g_arc, 0, 100);
    lv_obj_set_clickable(g_arc, false);
    lv_obj_remove_style(g_arc, NULL, LV_PART_KNOB);
    g_pct = lv_label_create(g_arc);
    lv_obj_set_style_text_font(g_pct, &lv_font_montserrat_28, 0);
    lv_obj_center(g_pct);

    lv_obj_t *facts = GuiPanel(top, "Kernel counters");
    lv_obj_set_flex_grow(facts, 1);
    lv_obj_set_height(facts, LV_PCT(100));
    g_total = GuiKeyValue(facts, "Total");
    g_used = GuiKeyValue(facts, "Used");
    g_free = GuiKeyValue(facts, "Free");
    g_lbx = GuiKeyValue(facts, "Objects awaiting reclaim (lbx)");

    lv_obj_t *history = GuiPanel(parent, "Used memory (MB), last 60 seconds");
    lv_obj_set_width(history, LV_PCT(100));
    lv_obj_set_flex_grow(history, 1);
    g_chart = lv_chart_create(history);
    lv_obj_set_size(g_chart, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_grow(g_chart, 1);
    lv_chart_set_type(g_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(g_chart, HISTORY);
    lv_chart_set_update_mode(g_chart, LV_CHART_UPDATE_MODE_SHIFT);
    lv_chart_set_axis_range(g_chart, LV_CHART_AXIS_PRIMARY_Y, 0,
                            (int32_t)(sp->mem_total_kb / 1024U));
    lv_obj_set_style_size(g_chart, 0, 0, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(g_chart, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(g_chart, 0, 0);
    g_series = lv_chart_add_series(g_chart, lv_color_hex(GUI_ACCENT), LV_CHART_AXIS_PRIMARY_Y);
    lv_chart_set_all_values(g_chart, g_series, LV_CHART_POINT_NONE);

    Sample(NULL);
    lv_timer_create(Sample, 1000, NULL);
}

int main(void)
{
    WinSpec spec = {.width = 760, .height = 540, .layer = WM_LAYER_NORMAL, .title = "Memory"};
    if (LvWinOpen(&spec) != ZUZU_OK)
        return 1;
    Build(UiRoot());
    LvWinRun();
    return 0;
}

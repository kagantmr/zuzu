#include "gui.h"

#include <net/nettypes.h>
#include <net/protocols/netd.h>
#include <string.h>
#include <zuzu/msg.h>
#include <zuzu/service.h>
#include <zuzu/uptime.h>
#include <zuzu/zuzu.h>

#define HISTORY 60
#define CHART_MIN_MAX 10

static const char *const k_stat_names[NIC_STAT_COUNT] = {
    [NIC_STAT_IRQ] = "Interrupts",
    [NIC_STAT_RX_PACKETS] = "RX packets",
    [NIC_STAT_TX_PACKETS] = "TX packets",
    [NIC_STAT_RX_RING_FULL] = "RX ring full",
    [NIC_STAT_RX_ERRORS] = "RX errors",
    [NIC_STAT_RX_OVERSIZE] = "RX oversize",
    [NIC_STAT_TX_DROPS] = "TX drops",
    [NIC_STAT_TX_TIMEOUTS] = "TX timeouts",
    [NIC_STAT_RX_OVERFLOWS] = "RX overflows",
    [NIC_STAT_TX_UNDERRUNS] = "TX underruns",
    [NIC_STAT_IRQ_OTHER] = "Other interrupts",
    [NIC_STAT_MAC_RX_BYTES] = "MAC RX bytes",
    [NIC_STAT_MAC_TX_BYTES] = "MAC TX bytes",
    [NIC_STAT_MAC_RX_FCS] = "MAC RX bad FCS",
    [NIC_STAT_MAC_RX_ALIGN] = "MAC RX alignment",
    [NIC_STAT_MAC_RX_RUNT] = "MAC RX runts",
    [NIC_STAT_MAC_RX_OVERSIZE] = "MAC RX oversize",
};

static Handle g_netd = -1;
static lv_timer_t *g_timer;
static lv_obj_t *g_name;
static lv_obj_t *g_link;
static lv_obj_t *g_ip;
static lv_obj_t *g_mask;
static lv_obj_t *g_gw;
static lv_obj_t *g_dns;
static lv_obj_t *g_mac;
static lv_obj_t *g_rate;
static lv_obj_t *g_chart_title;
static lv_obj_t *g_chart;
static lv_chart_series_t *g_rx;
static lv_chart_series_t *g_tx;
static lv_obj_t *g_table;
static NetdNetif g_prev;
static uint64_t g_prev_ms;
static bool g_have_prev;
static bool g_bytes;

static Err GetNetif(NetdNetif *nif)
{
    if (g_netd < 0) {
        g_netd = LookupService("/svc/netd");
        if (g_netd < 0)
            return ERR_NOENT;
    }
    uint8_t op = NETD_GET_NETIF;
    MsgWrite(&op, sizeof(op));
    uint32_t got = 0;
    Err rc = CallPort(g_netd, sizeof(op), -1, &got, NULL);
    if (rc != ZUZU_OK) {
        HandleClose(g_netd);
        g_netd = -1;
        return rc;
    }
    if (got < sizeof(*nif))
        return ERR_MALFORMED;
    memcpy(nif, GetMessageBox(), sizeof(*nif));
    return ZUZU_OK;
}

static void SetIp(lv_obj_t *label, Ipv4Addr ip)
{
    char buf[20];
    Ipv4ToStr(ip, buf);
    lv_label_set_text(label, buf);
}

static uint32_t PerSecond(uint32_t now, uint32_t before, uint64_t ms)
{
    return ms ? (uint32_t)((uint64_t)(now - before) * 1000U / ms) : 0;
}

static void Rescale(void)
{
    int32_t top = CHART_MIN_MAX;
    for (uint32_t i = 0; i < HISTORY; i++) {
        int32_t a = lv_chart_get_series_y_array(g_chart, g_rx)[i];
        int32_t b = lv_chart_get_series_y_array(g_chart, g_tx)[i];
        if (a != LV_CHART_POINT_NONE && a > top)
            top = a;
        if (b != LV_CHART_POINT_NONE && b > top)
            top = b;
    }
    lv_chart_set_axis_range(g_chart, LV_CHART_AXIS_PRIMARY_Y, 0, top + top / 5);
}

static void FillTable(const NetdNetif *n)
{
    uint32_t row = 0;
    for (uint32_t i = 0; i < NIC_STAT_COUNT; i++) {
        if (!k_stat_names[i])
            continue;
        lv_table_set_cell_value(g_table, row, 0, k_stat_names[i]);
        lv_table_set_cell_value_fmt(g_table, row, 1, "%u", (unsigned)n->stat[i]);
        row++;
    }
}

static void Sample(lv_timer_t *timer)
{
    (void)timer;
    NetdNetif n;
    if (GetNetif(&n) != ZUZU_OK) {
        lv_label_set_text(g_link, "netd not reachable");
        lv_obj_set_style_text_color(g_link, lv_color_hex(GUI_BAD), 0);
        return;
    }
    uint64_t now = UptimeMs();

    lv_label_set_text(g_name, n.name[0] ? n.name : "-");
    lv_label_set_text(g_link, n.link_up ? "up" : "down");
    lv_obj_set_style_text_color(g_link, lv_color_hex(n.link_up ? GUI_GOOD : GUI_BAD), 0);
    SetIp(g_ip, n.ip);
    SetIp(g_mask, n.netmask);
    SetIp(g_gw, n.gateway);
    SetIp(g_dns, n.dns);
    lv_label_set_text_fmt(g_mac, "%02x:%02x:%02x:%02x:%02x:%02x", n.mac[0], n.mac[1], n.mac[2],
                          n.mac[3], n.mac[4], n.mac[5]);
    FillTable(&n);

    bool bytes = n.stat[NIC_STAT_MAC_RX_BYTES] != 0 || n.stat[NIC_STAT_MAC_TX_BYTES] != 0;
    if (bytes != g_bytes) {
        g_bytes = bytes;
        g_have_prev = false;
        lv_chart_set_all_values(g_chart, g_rx, LV_CHART_POINT_NONE);
        lv_chart_set_all_values(g_chart, g_tx, LV_CHART_POINT_NONE);
        lv_label_set_text(g_chart_title, bytes ? "Throughput (KB/s), last 60 seconds"
                                               : "Packets per second, last 60 seconds");
    }

    if (g_have_prev) {
        uint64_t ms = now - g_prev_ms;
        uint32_t rx;
        uint32_t tx;
        if (g_bytes) {
            rx = PerSecond(n.stat[NIC_STAT_MAC_RX_BYTES], g_prev.stat[NIC_STAT_MAC_RX_BYTES], ms);
            tx = PerSecond(n.stat[NIC_STAT_MAC_TX_BYTES], g_prev.stat[NIC_STAT_MAC_TX_BYTES], ms);
            lv_label_set_text_fmt(g_rate, "RX %u.%u KB/s   TX %u.%u KB/s", (unsigned)(rx / 1024U),
                                  (unsigned)(rx % 1024U * 10U / 1024U), (unsigned)(tx / 1024U),
                                  (unsigned)(tx % 1024U * 10U / 1024U));
            rx /= 1024U;
            tx /= 1024U;
        } else {
            rx = PerSecond(n.stat[NIC_STAT_RX_PACKETS], g_prev.stat[NIC_STAT_RX_PACKETS], ms);
            tx = PerSecond(n.stat[NIC_STAT_TX_PACKETS], g_prev.stat[NIC_STAT_TX_PACKETS], ms);
            lv_label_set_text_fmt(g_rate, "RX %u pkt/s   TX %u pkt/s", (unsigned)rx, (unsigned)tx);
        }
        lv_chart_set_next_value(g_chart, g_rx, (int32_t)rx);
        lv_chart_set_next_value(g_chart, g_tx, (int32_t)tx);
        Rescale();
    }
    g_prev = n;
    g_prev_ms = now;
    g_have_prev = true;
}

static lv_obj_t *Legend(lv_obj_t *parent, const char *text, uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text_fmt(label, LV_SYMBOL_MINUS " %s", text);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    return label;
}

static void Open(lv_obj_t *parent)
{
    g_have_prev = false;
    g_bytes = false;
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(parent, 16, 0);

    lv_obj_t *left = lv_obj_create(parent);
    lv_obj_remove_style_all(left);
    lv_obj_set_size(left, 330, LV_PCT(100));
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(left, 16, 0);

    lv_obj_t *info = GuiPanel(left, "Interface");
    lv_obj_set_width(info, LV_PCT(100));
    g_name = GuiKeyValue(info, "Name");
    g_link = GuiKeyValue(info, "Link");
    g_ip = GuiKeyValue(info, "Address");
    g_mask = GuiKeyValue(info, "Netmask");
    g_gw = GuiKeyValue(info, "Gateway");
    g_dns = GuiKeyValue(info, "DNS");
    g_mac = GuiKeyValue(info, "MAC");

    lv_obj_t *counters = GuiPanel(left, "Driver counters");
    lv_obj_set_width(counters, LV_PCT(100));
    lv_obj_set_flex_grow(counters, 1);
    lv_obj_set_scrollable(counters, true);
    g_table = lv_table_create(counters);
    lv_table_set_column_count(g_table, 2);
    lv_table_set_column_width(g_table, 0, 190);
    lv_table_set_column_width(g_table, 1, 100);
    lv_obj_set_style_pad_ver(g_table, 4, LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(g_table, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(g_table, LV_OPA_TRANSP, LV_PART_ITEMS);
    lv_obj_set_style_text_color(g_table, lv_color_hex(GUI_TEXT), LV_PART_ITEMS);
    lv_obj_set_style_border_width(g_table, 0, 0);
    lv_obj_set_style_border_width(g_table, 0, LV_PART_ITEMS);

    lv_obj_t *traffic = GuiPanel(parent, NULL);
    lv_obj_set_height(traffic, LV_PCT(100));
    lv_obj_set_flex_grow(traffic, 1);
    g_chart_title = lv_label_create(traffic);
    lv_label_set_text(g_chart_title, "Packets per second, last 60 seconds");
    lv_obj_set_style_text_color(g_chart_title, lv_color_hex(GUI_DIM), 0);
    g_rate = lv_label_create(traffic);
    lv_label_set_text(g_rate, "measuring...");
    lv_obj_set_style_text_font(g_rate, &lv_font_montserrat_20, 0);

    lv_obj_t *legend = lv_obj_create(traffic);
    lv_obj_remove_style_all(legend);
    lv_obj_set_size(legend, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(legend, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(legend, 24, 0);
    Legend(legend, "received", GUI_GOOD);
    Legend(legend, "sent", GUI_ACCENT);

    g_chart = lv_chart_create(traffic);
    lv_obj_set_width(g_chart, LV_PCT(100));
    lv_obj_set_flex_grow(g_chart, 1);
    lv_chart_set_type(g_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(g_chart, HISTORY);
    lv_chart_set_update_mode(g_chart, LV_CHART_UPDATE_MODE_SHIFT);
    lv_chart_set_axis_range(g_chart, LV_CHART_AXIS_PRIMARY_Y, 0, CHART_MIN_MAX);
    lv_obj_set_style_size(g_chart, 0, 0, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(g_chart, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(g_chart, 0, 0);
    g_rx = lv_chart_add_series(g_chart, lv_color_hex(GUI_GOOD), LV_CHART_AXIS_PRIMARY_Y);
    g_tx = lv_chart_add_series(g_chart, lv_color_hex(GUI_ACCENT), LV_CHART_AXIS_PRIMARY_Y);
    lv_chart_set_all_values(g_chart, g_rx, LV_CHART_POINT_NONE);
    lv_chart_set_all_values(g_chart, g_tx, LV_CHART_POINT_NONE);

    Sample(NULL);
    g_timer = lv_timer_create(Sample, 1000, NULL);
}

static void Close(void)
{
    if (g_timer)
        lv_timer_delete(g_timer);
    g_timer = NULL;
}

const GuiApp g_app_netdiag = {
    .name = "Netdiag",
    .symbol = LV_SYMBOL_WIFI,
    .color = GUI_GOOD,
    .open = Open,
    .close = Close,
};

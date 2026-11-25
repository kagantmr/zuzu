#include "wm.h"

#include <string.h>
#include <util/font12x16.h>

#include "logo.h"

#define LOG_TAG "wm"
#include <util/log.h>

#define FADE_MS 900U
#define WAVE_MS 1500U
#define GIVE_UP_MS 30000U
#define PANEL_WAIT_MS 3000U
#define DOTS 5
#define DOT_D 12
#define DOT_GAP 14
#define DOT_SUB 4
#define LOGO_LIFT 70
#define DOTS_BELOW 56
#define STATUS_BELOW 40
#define COL_BG 0x000000U
#define COL_DOT_DIM 0x123047U
#define COL_DOT_LIT 0x4fc3f7U
#define COL_STATUS 0x8b98a5U

bool g_booting = true;

static uint8_t g_dot[DOT_D * DOT_D];
static uint64_t g_start;
static uint64_t g_now;
static char g_status[WM_TITLE_MAX] = "Starting zuzu";
static uint32_t g_logo_opacity;
static uint64_t g_finish_at;

static Rect LogoRect(void)
{
    return (Rect){(g_screen_w - LOGO_W) / 2, (g_screen_h - LOGO_H) / 2 - LOGO_LIFT, LOGO_W, LOGO_H};
}

static Rect DotsRect(void)
{
    Rect logo = LogoRect();
    int32_t w = (DOTS * DOT_D) + ((DOTS - 1) * DOT_GAP);
    return (Rect){(g_screen_w - w) / 2, logo.y + logo.h + DOTS_BELOW, w, DOT_D};
}

static Rect StatusRect(void)
{
    Rect dots = DotsRect();
    return (Rect){0, dots.y + dots.h + STATUS_BELOW, g_screen_w, FONT_GLYPH_H};
}

static void MakeDot(void)
{
    int32_t r2 = (DOT_D * DOT_SUB / 2) * (DOT_D * DOT_SUB / 2);
    int32_t c = DOT_D * DOT_SUB / 2;
    for (int32_t y = 0; y < DOT_D; y++) {
        for (int32_t x = 0; x < DOT_D; x++) {
            uint32_t hits = 0;
            for (int32_t sy = 0; sy < DOT_SUB; sy++) {
                for (int32_t sx = 0; sx < DOT_SUB; sx++) {
                    int32_t dx = (x * DOT_SUB) + sx - c;
                    int32_t dy = (y * DOT_SUB) + sy - c;
                    if ((dx * dx) + (dy * dy) < r2)
                        hits++;
                }
            }
            g_dot[(y * DOT_D) + x] = (uint8_t)(hits * 255U / (DOT_SUB * DOT_SUB));
        }
    }
}

static uint32_t DotLevel(int i)
{
    uint32_t t = (uint32_t)((g_now - g_start) % WAVE_MS);
    int32_t centre = (int32_t)(t * (DOTS + 4) / WAVE_MS) - 2;
    int32_t frac = (int32_t)((t * (DOTS + 4) * 256U / WAVE_MS) % 256U);
    int32_t pos = (centre * 256) + frac;
    int32_t d = (i * 256) - pos;
    if (d < 0)
        d = -d;
    return d >= 512 ? 0U : (uint32_t)(255 - (d * 255 / 512));
}

static bool PanelShown(void)
{
    for (int i = 0; i < (int)WM_MAX_WINDOWS; i++) {
        if (WinVisible(&g_win[i]) && g_win[i].layer == WM_LAYER_PANEL)
            return true;
    }
    return false;
}

void SplashFinish(void)
{
    if (g_booting && !g_finish_at)
        g_finish_at = g_now ? g_now : 1;
}

void SplashStart(uint64_t now)
{
    g_start = now;
    g_now = now;
    MakeDot();
    DamageAll();
}

bool SplashTick(uint64_t now)
{
    if (!g_booting)
        return false;
    g_now = now;
    if (g_finish_at && (PanelShown() || now - g_finish_at >= PANEL_WAIT_MS)) {
        SplashEnd();
        return false;
    }
    if (now - g_start >= GIVE_UP_MS) {
        LOG_WARN(LOG_TAG, "boot never finished, showing the desktop");
        SplashEnd();
        return false;
    }
    uint32_t opacity =
        now - g_start >= FADE_MS ? 255U : (uint32_t)((now - g_start) * 255U / FADE_MS);
    if (opacity != g_logo_opacity) {
        g_logo_opacity = opacity;
        Damage(LogoRect());
    }
    Damage(DotsRect());
    return true;
}

void SplashPaint(Rect clip)
{
    Fill(clip, clip, Lerp565(COL_BG, COL_BG, 0, 1));
    Rect logo = LogoRect();
    Blend(clip, logo.x, logo.y, k_logo, LOGO_W, LOGO_H, 0xFFFFFFU, g_logo_opacity);

    Rect dots = DotsRect();
    for (int i = 0; i < DOTS; i++) {
        uint32_t level = DotLevel(i);
        uint16_t c565 = Lerp565(COL_DOT_DIM, COL_DOT_LIT, (int32_t)level, 255);
        uint32_t rgb = ((uint32_t)((c565 >> 11) & 0x1FU) << 19) |
                       ((uint32_t)((c565 >> 5) & 0x3FU) << 10) | ((uint32_t)(c565 & 0x1FU) << 3);
        Blend(clip, dots.x + (i * (DOT_D + DOT_GAP)), dots.y, g_dot, DOT_D, DOT_D, rgb, 255U);
    }

    Rect status = StatusRect();
    int32_t w = (int32_t)strlen(g_status) * FONT_GLYPH_W;
    Text(clip, (g_screen_w - w) / 2, status.y, g_status, w, Lerp565(COL_STATUS, COL_STATUS, 0, 1));
}

void SplashStatus(const char *text)
{
    strncpy(g_status, text, sizeof(g_status) - 1);
    g_status[sizeof(g_status) - 1] = '\0';
    if (g_booting)
        Damage(StatusRect());
}

void SplashEnd(void)
{
    if (!g_booting)
        return;
    g_booting = false;
    LOG_INFO(LOG_TAG, "boot done after %u ms", (unsigned)(g_now - g_start));
    DamageAll();
}

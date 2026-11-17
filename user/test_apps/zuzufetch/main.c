#include <ansi.h>
#include <dev/protocols/fb.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <util/version.h>
#include <zuzu/msg.h>
#include <zuzu/service.h>
#include <zuzu/syspage.h>
#include <zuzu/uptime.h>
#include <zuzu/zuzu.h>

#define LOGO_WIDTH 50
#define INFO_MAX 28
#define INFO_LINE_LEN 96
#define LABEL_WIDTH 9
#define BAR_CELLS 14
#define FBCONS_CELL_W 12
#define FBCONS_CELL_H 16

#define COLOR_LABEL ANSI_BOLD ANSI_CYAN
#define COLOR_OK "\033[92m"
#define COLOR_WARN "\033[93m"
#define COLOR_BAD "\033[91m"

#if defined(__ARM_PCS_VFP)
#define FLOAT_ABI "hard-float"
#else
#define FLOAT_ABI "soft-float"
#endif

static void Kv(char *dst, size_t cap, const char *label, const char *value)
{
    snprintf(dst, cap, COLOR_LABEL "%-*s" ANSI_RESET "%s", LABEL_WIDTH, label, value);
}

static int VisibleLen(const char *s)
{
    int len = 0;
    while (*s) {
        if (*s == '\033') {
            while (*s && *s != 'm')
                s++;
            if (*s == 'm')
                s++;
        } else {
            len++;
            s++;
        }
    }
    return len;
}

static void FormatUptime(char *dst, size_t cap, uint64_t secs)
{
    uint64_t d = secs / 86400, h = (secs / 3600) % 24, m = (secs / 60) % 60, sec = secs % 60;
    if (d > 0)
        snprintf(dst, cap, "%llud %lluh %llum", (unsigned long long)d, (unsigned long long)h,
                 (unsigned long long)m);
    else if (h > 0)
        snprintf(dst, cap, "%lluh %llum %llus", (unsigned long long)h, (unsigned long long)m,
                 (unsigned long long)sec);
    else if (m > 0)
        snprintf(dst, cap, "%llum %llus", (unsigned long long)m, (unsigned long long)sec);
    else
        snprintf(dst, cap, "%llus", (unsigned long long)sec);
}

static void FormatBar(char *dst, size_t cap, unsigned pct)
{
    unsigned fill = ((pct * BAR_CELLS) + 50U) / 100U;
    if (fill > BAR_CELLS)
        fill = BAR_CELLS;
    const char *color = pct < 60 ? COLOR_OK : (pct < 85 ? COLOR_WARN : COLOR_BAD);
    char on[BAR_CELLS + 1];
    char off[BAR_CELLS + 1];
    memset(on, '#', fill);
    on[fill] = '\0';
    memset(off, '-', BAR_CELLS - fill);
    off[BAR_CELLS - fill] = '\0';
    snprintf(dst, cap, "[%s%s" ANSI_GRAY "%s" ANSI_RESET "] %u%%", color, on, off, pct);
}

static void CompilerName(char *dst, size_t cap)
{
    const char *v = __VERSION__;
    size_t n = 0;
    while (v[n] && v[n] != ' ')
        n++;
    snprintf(dst, cap, "GCC %.*s", (int)n, v);
}

static bool QueryDisplay(FbInfo *info)
{
    Handle gfx = LookupService("/svc/gfx");
    if (gfx < 0)
        return false;

    FbRequest req = {.cmd = FB_CMD_GET_INFO};
    MsgWrite(&req, sizeof(req));
    uint32_t got = 0;
    Err rc = CallPort(gfx, sizeof(req), -1, &got, NULL);
    HandleClose(gfx);
    if (rc != ZUZU_OK || got < sizeof(*info))
        return false;
    memcpy(info, GetMessageBox(), sizeof(*info));
    return info->status == ZUZU_OK;
}

static void EmitTiles(char *dst, size_t cap, int bright)
{
    size_t off = 0;
    for (int c = 0; c < 8 && off < cap; c++)
        off += (size_t)snprintf(dst + off, cap - off, "\033[%dm  ", (bright ? 100 : 40) + c);
    snprintf(dst + off, cap - off, ANSI_RESET);
}

static int BuildInfo(char info[][INFO_LINE_LEN])
{
    int n = 0;
    char tmp[64];
    Syspage *sp = (Syspage *)SYSPAGE;

    info[n++][0] = '\0';

    snprintf(info[n++], INFO_LINE_LEN, ANSI_BOLD ANSI_CYAN "zuzu" ANSI_RESET ANSI_GRAY "@" ANSI_RESET
             ANSI_BOLD "%s" ANSI_RESET, sp->machine);
    char rule[40];
    size_t rl = 5 + strlen(sp->machine);
    if (rl > sizeof(rule) - 1)
        rl = sizeof(rule) - 1;
    memset(rule, '-', rl);
    rule[rl] = '\0';
    snprintf(info[n++], INFO_LINE_LEN, ANSI_GRAY "%s" ANSI_RESET, rule);

    snprintf(tmp, sizeof(tmp), "zuzuOS %s", ZUZUOS_VERSION);
    Kv(info[n++], INFO_LINE_LEN, "OS", tmp);
    Kv(info[n++], INFO_LINE_LEN, "Kernel", sp->version);
    Kv(info[n++], INFO_LINE_LEN, "Build", sp->build);
    Kv(info[n++], INFO_LINE_LEN, "Machine", sp->machine);
    Kv(info[n++], INFO_LINE_LEN, "CPU", sp->cpu);

    snprintf(tmp, sizeof(tmp), "ARMv%d-A, " FLOAT_ABI, (int)__ARM_ARCH);
    Kv(info[n++], INFO_LINE_LEN, "Arch", tmp);

    uint64_t hz = sp->counter_freq;
    snprintf(tmp, sizeof(tmp), "%u.%02u MHz", (unsigned)(hz / 1000000U),
             (unsigned)((hz / 10000U) % 100U));
    Kv(info[n++], INFO_LINE_LEN, "Timer", tmp);

    char up[32];
    FormatUptime(up, sizeof(up), UptimeMs() / 1000U);
    Kv(info[n++], INFO_LINE_LEN, "Uptime", up);

    unsigned total_mb = (unsigned)(sp->mem_total_kb / 1024U);
    unsigned free_mb = (unsigned)(sp->mem_free_kb / 1024U);
    unsigned used_mb = total_mb > free_mb ? total_mb - free_mb : 0;
    unsigned pct = total_mb ? (used_mb * 100U) / total_mb : 0;
    FormatBar(tmp, sizeof(tmp), pct);
    Kv(info[n++], INFO_LINE_LEN, "Memory", tmp);
    snprintf(tmp, sizeof(tmp), "%u / %u MB used", used_mb, total_mb);
    Kv(info[n++], INFO_LINE_LEN, "", tmp);

    FbInfo fb;
    if (QueryDisplay(&fb)) {
        snprintf(tmp, sizeof(tmp), "%ux%u %s", (unsigned)fb.width, (unsigned)fb.height,
                 fb.format == FB_FMT_RGB565 ? "RGB565" : "unknown");
        Kv(info[n++], INFO_LINE_LEN, "Display", tmp);
        snprintf(tmp, sizeof(tmp), "fbcons %ux%u", (unsigned)(fb.width / FBCONS_CELL_W),
                 (unsigned)(fb.height / FBCONS_CELL_H));
        Kv(info[n++], INFO_LINE_LEN, "Terminal", tmp);
    }

    if (sp->dev_count > 0) {
        snprintf(tmp, sizeof(tmp), "%u", (unsigned)sp->dev_count);
        Kv(info[n++], INFO_LINE_LEN, "Devices", tmp);
    }

    CompilerName(tmp, sizeof(tmp));
    Kv(info[n++], INFO_LINE_LEN, "Compiler", tmp);

    info[n++][0] = '\0';
    EmitTiles(info[n++], INFO_LINE_LEN, 0);
    EmitTiles(info[n++], INFO_LINE_LEN, 1);
    return n;
}

int main(void)
{
    static const char *logo[] = {
        "                                                  ",
        "                                                  ",
        "       \033[37m@@@@@@@@@@@\033[0m                                ",
        "       \033[37m@@@@@@@@@@@\033[0m                \033[90m@@\033[0m              ",
        "            \033[37m@@@@\033[0m                \033[90m@@@@@    @@@\033[0m      ",
        "          \033[37m@@@@\033[0m                  \033[90m@@@       @@@\033[0m     ",
        "        \033[37m@@@@\033[0m                    \033[90m@@@      @@@\033[0m      ",
        "      \033[37m@@@@\033[0m                     \033[90m@@@       @@\033[0m       ",
        "    \033[37m@@@@\033[0m           \033[37m@@@@@@@@\033[0m    \033[90m@@@      @@@\033[0m  "
        "     ",
        "   \033[37m@@@@@@@@@@@@@@@@@\033[0m      \033[37m@@@\033[0m  \033[90m@@@     @@@\033[0m   "
        "     ",
        "            \033[37m@@\033[0m                 \033[90m@@@@@@@@\033[0m           ",
        "           \033[37m@@@\033[0m                                    ",
        "           \033[37m@@@\033[0m                       \033[90m@@@\033[0m          ",
        "           \033[37m@@@\033[0m      \033[90m@@@@@@@@@@@\033[0m     \033[90m@@@\033[0m      "
        "     ",
        "           \033[37m@@@\033[0m    \033[90m@@@         @@@\033[0m   \033[90m@@@\033[0m      "
        "     ",
        "           \033[37m@@@\033[0m    \033[90m@@@         @@@\033[0m   \033[90m@@@\033[0m      "
        "     ",
        "           \033[37m@@@\033[0m   \033[90m@@@          @@@\033[0m   \033[90m@@@\033[0m      "
        "     ",
        "            \033[37m@@@@@@@@\033[0m           \033[90m@@@@@@@@\033[0m           ",
        "                                                  ",
        "                                                  ",
        "                                                  "};
    static char info[INFO_MAX][INFO_LINE_LEN];
    int info_count = BuildInfo(info);

    int logo_count = (int)(sizeof(logo) / sizeof(logo[0]));
    int lines = logo_count > info_count ? logo_count : info_count;

    for (int i = 0; i < lines; i++) {
        if (i < logo_count) {
            printf("%s%s%s", ANSI_CYAN, logo[i], ANSI_RESET);
            int pad = LOGO_WIDTH - VisibleLen(logo[i]);
            for (int p = 0; p < pad; p++)
                printf(" ");
        } else {
            for (int p = 0; p < LOGO_WIDTH; p++)
                printf(" ");
        }

        printf("  ");
        if (i < info_count)
            printf("%s", info[i]);
        printf("\n");
    }

    printf(ANSI_RESET);
    return 0;
}

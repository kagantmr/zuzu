#include <stdint.h>

#include "core/kprintf.h"
#include "core/version.h"
#include "kernel/dtb/dtb.h"
#include "kernel/time/tick.h"
#include "kernel/mm/pmm.h"
#include "arch/arm/include/symbols.h"
#include <string.h>
#include <snprintf.h>
#include "layout.h"

#ifndef ZUZU_BANNER_SHOW_ADDR
#define ZUZU_BANNER_SHOW_ADDR 0
#endif

#define ANSI_RESET "\033[0m"
#define ANSI_CYAN  "\033[1;36m"
#define ANSI_BLUE  "\033[1;34m"

#define LOGO_WIDTH    38
#define INFO_MAX      20
#define INFO_LINE_LEN 80
#define LABEL_WIDTH   10

extern kernel_layout_t kernel_layout;
extern pmm_state_t pmm_state;

/* Write "Label:    value" into dst with ANSI color on the label */
static void fmt_kv(char *dst, size_t cap, const char *label, const char *value)
{
    char padded[LABEL_WIDTH + 1];
    snprintf(padded, sizeof(padded), "%-*s", LABEL_WIDTH, label);
    snprintf(dst, cap, ANSI_BLUE "%s" ANSI_RESET "%s", padded, value);
}

static void emit_tiles(char *dst, size_t cap)
{
    snprintf(dst, cap,
        "\033[40m  \033[0m\033[41m  \033[0m\033[42m  \033[0m\033[43m  \033[0m"
        "\033[44m  \033[0m\033[45m  \033[0m\033[46m  \033[0m\033[47m  \033[0m");
}

/* ── build info lines ────────────────────────────────────────── */

/* ── device probe ────────────────────────────────────────────── */

static const struct {
    const char *compatible;
    const char *label;      /* left column */
    const char *name;       /* right column */
} device_probes[] = {
    { "smsc,lan9118",       "NIC:",     "SMSC LAN9118"  },
    { "smsc,lan9220",       "NIC:",     "SMSC LAN9220"  },
    { "arm,pl111",          "Display:", "PL111 CLCD"     },
    { "arm,pl050",          "Input:",   "PL050 KMI"      },
    { "arm,pl041",          "Audio:",   "PL041 AACI"     },
    { "arm,pl031",          "RTC:",     "PL031"          },
    { "arm,pl181",          "Storage:", "PL181 MCI"      },
    { NULL, NULL, NULL }
};


/* ── build info lines ────────────────────────────────────────── */

static int build_info(char info[][INFO_LINE_LEN])
{
    int n = 0;
    char tmp[64];

    /* blank lines to align with logo top */
    info[n][0] = '\0'; n++;
    info[n][0] = '\0'; n++;

    /* title */
    snprintf(info[n], INFO_LINE_LEN,
             ANSI_CYAN "zuzu" ANSI_RESET " %s", ZUZU_VERSION);
    n++;

    snprintf(info[n], INFO_LINE_LEN,
             ANSI_CYAN "----------" ANSI_RESET);
    n++;

    /* machine */
    char machine[64] = "Unknown";
    dtb_get_string("/", "model", machine, sizeof(machine));
    fmt_kv(info[n], INFO_LINE_LEN, "Machine:", machine);
    n++;

    /* cpu */
    fmt_kv(info[n], INFO_LINE_LEN, "CPU:", "ARM Cortex-A15");
    n++;

    /* memory */
    uint32_t ram_mb  = (uint32_t)((pmm_state.total_pages * (uint64_t)PAGE_SIZE) / 1024 / 1024);
    uint32_t free_mb = (uint32_t)((pmm_state.free_pages  * (uint64_t)PAGE_SIZE) / 1024 / 1024);
    snprintf(tmp, sizeof(tmp), "%u MB free / %u MB total", free_mb, ram_mb);
    fmt_kv(info[n], INFO_LINE_LEN, "Memory:", tmp);
    n++;

    /* timer */
    snprintf(tmp, sizeof(tmp), "%u Hz", get_tick_rate());
    fmt_kv(info[n], INFO_LINE_LEN, "Timer:", tmp);
    n++;

    /* build */
    snprintf(tmp, sizeof(tmp), "%s %s", __DATE__, __TIME__);
    fmt_kv(info[n], INFO_LINE_LEN, "Build:", tmp);
    n++;

    /* devices */
    char path[128];
    for (int i = 0; device_probes[i].compatible; i++) {
        if (dtb_find_compatible(device_probes[i].compatible, path, sizeof(path))) {
            fmt_kv(info[n], INFO_LINE_LEN,
                   device_probes[i].label,
                   device_probes[i].name);
            n++;
        }
    }

#if ZUZU_BANNER_SHOW_ADDR
    snprintf(tmp, sizeof(tmp), "%P - %P", _kernel_start, _kernel_end);
    fmt_kv(info[n], INFO_LINE_LEN, "Kernel:", tmp);
    n++;

    snprintf(tmp, sizeof(tmp), "%P - %P",
             (void *)kernel_layout.heap_start_va,
             (void *)kernel_layout.heap_end_va);
    fmt_kv(info[n], INFO_LINE_LEN, "Heap:", tmp);
    n++;

    snprintf(tmp, sizeof(tmp), "%P - %P",
             (void *)kernel_layout.stack_base_va,
             (void *)kernel_layout.stack_top_va);
    fmt_kv(info[n], INFO_LINE_LEN, "Stack:", tmp);
    n++;
#endif

    /* pmm */
    snprintf(tmp, sizeof(tmp), "%u free / %u pages",
             (unsigned)pmm_state.free_pages,
             (unsigned)pmm_state.total_pages);
    fmt_kv(info[n], INFO_LINE_LEN, "PMM:", tmp);
    n++;

    /* blank spacer */
    info[n][0] = '\0'; n++;

    /* color tiles */
    emit_tiles(info[n], INFO_LINE_LEN);
    n++;

    return n;
}


void print_boot_banner(void)
{
    static const char *logo[] = {
        "",
        "        \033[37m@@@@@@@@\033[0m",
        "        \033[90m####\033[37m@@@@\033[0m            \033[90m@      -\033[0m",
        "           \033[37m@@@\033[0m             \033[37m@@@@@\033[0m  \033[90m@@.\033[0m",
        "          \033[37m@@\033[0m               \033[37m@@\033[0m     \033[37m@@\033[0m",
        "        \033[37m@@@\033[0m               \033[90m@@@\033[0m    \033[37m@@@\033[0m",
        "      \033[37m@@@\033[0m                 \033[90m@@\033[0m     \033[37m@@\033[0m",
        "     \033[37m@@@@@@@@@@@@@@@@@@@\033[90m-\033[0m \033[37m@@\033[0m    \033[90m@@@\033[0m",
        "                          \033[37m@@@@@@@@\033[0m",
        "           \033[37m@@\033[0m",
        "           \033[37m@@\033[0m                 \033[37m@@\033[0m",
        "           \033[37m@@\033[0m     \033[90m@@@@@@@\033[0m     \033[37m@@\033[0m",
        "           \033[37m@@\033[0m   \033[37m@@\033[0m      \033[90m-@@@\033[0m  \033[37m@@\033[0m",
        "           \033[37m@@\033[0m  \033[90m=\033[37m@@\033[0m       \033[90m@@@\033[0m  \033[37m@@\033[0m",
        "           \033[37m@@\033[0m  \033[90m@@@\033[0m       \033[90m@@@\033[0m  \033[37m@@\033[0m",
        "           \033[37m@@@@@@\033[0m         \033[37m@@@@@\033[0m",
        "",
    };

    static char info[INFO_MAX][INFO_LINE_LEN];
    int info_count = build_info(info);

    int logo_count = (int)(sizeof(logo) / sizeof(logo[0]));
    int lines = logo_count > info_count ? logo_count : info_count;

    for (int i = 0; i < lines; i++) {
        /* logo column */
        if (i < logo_count) {
            kprintf("%s%s%s", ANSI_CYAN, logo[i], ANSI_RESET);
            int pad = LOGO_WIDTH - visible_len(logo[i]);
            for (int p = 0; p < pad; p++)
                kprintf(" ");
        } else {
            for (int p = 0; p < LOGO_WIDTH; p++)
                kprintf(" ");
        }

        /* info column */
        kprintf("  ");
        if (i < info_count)
            kprintf("%s", info[i]);

        kprintf("\n");
    }

    kprintf(ANSI_RESET);
}
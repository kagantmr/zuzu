#include <stdint.h>

// include whatever your kernel uses
#include "core/kprintf.h"
#include "kernel/dtb/dtb.h"
#include "arch/arm/timer/generic_timer.h"
#include "kernel/time/tick.h"
#include "arch/arm/include/symbols.h"
#include "core/version.h"
#include "kernel/mm/pmm.h"
#include "lib/mem.h"
#include "lib/snprintf.h"
#include "layout.h"

#ifndef ZUZU_BANNER_SHOW_ADDR
#define ZUZU_BANNER_SHOW_ADDR 0
#endif

// ANSI
#define ANSI_RESET   "\033[0m"
#define ANSI_WHITE   "\033[1;37m"
#define ANSI_CYAN    "\033[1;36m"
#define ANSI_BLUE    "\033[1;34m"

extern kernel_layout_t kernel_layout;
extern pmm_state_t pmm_state;
extern phys_region_t phys_region;

static inline uint32_t read_be32(const void *p)
{
    const uint8_t *b = (const uint8_t *)p;
    return ((uint32_t)b[0] << 24) |
           ((uint32_t)b[1] << 16) |
           ((uint32_t)b[2] << 8) |
           ((uint32_t)b[3]);
}

// Count visible characters (skip ANSI escape sequences).
static int visible_len(const char *s)
{
    int len = 0;
    while (*s) {
        if (*s == '\033') {
            while (*s && *s != 'm') s++;
            if (*s) s++;
        } else {
            len++;
            s++;
        }
    }
    return len;
}

// Emit a logo line padded to LOGO_WIDTH visible chars. Does not emit newline.
#define LOGO_WIDTH 38

static void emit_logo_line(const char *line)
{
    kprintf("%s%s%s", ANSI_CYAN, line, ANSI_RESET);
    int pad = LOGO_WIDTH - visible_len(line);
    for (int i = 0; i < pad; i++)
        kprintf(" ");
}

static void emit_tiles(void)
{
    kprintf("\033[40m  \033[0m");
    kprintf("\033[41m  \033[0m");
    kprintf("\033[42m  \033[0m");
    kprintf("\033[43m  \033[0m");
    kprintf("\033[44m  \033[0m");
    kprintf("\033[45m  \033[0m");
    kprintf("\033[46m  \033[0m");
    kprintf("\033[47m  \033[0m");
}

#define INFO_LABEL_WIDTH 10

static void emit_info_kv(const char *label, const char *value)
{
    char padded[INFO_LABEL_WIDTH + 1];
    snprintf(padded, sizeof(padded), "%-*s", INFO_LABEL_WIDTH, label);
    kprintf("%s%s%s%s", ANSI_BLUE, padded, ANSI_RESET, value);
}

static void emit_info_line(int i)
{
    char val[64];

    switch (i) {
        case 0:
        case 1:
            break;

        case 2:
            kprintf("%szuzu%s %s", ANSI_CYAN, ANSI_RESET, ZUZU_VERSION);
            break;

        case 3:
            kprintf("%s----------%s", ANSI_CYAN, ANSI_RESET);
            break;

        case 4: {
            char machine[64] = "Unknown";
            (void)dtb_get_string("/", "model", machine, sizeof(machine));
            emit_info_kv("Machine:", machine);
            break;
        }

        case 5:
            emit_info_kv("CPU:", "ARM Cortex-A15");
            break;

        case 6: {
            uint32_t ram_mb  = (uint32_t)((pmm_state.total_pages * (uint64_t)PAGE_SIZE) / 1024 / 1024);
            uint32_t free_mb = (uint32_t)((pmm_state.free_pages  * (uint64_t)PAGE_SIZE) / 1024 / 1024);
            snprintf(val, sizeof(val), "%u MB free / %u MB total", free_mb, ram_mb);
            emit_info_kv("Memory:", val);
            break;
        }

        case 7:
            snprintf(val, sizeof(val), "%u Hz", get_tick_rate());
            emit_info_kv("Timer:", val);
            break;

        case 8:
            snprintf(val, sizeof(val), "%s %s", __DATE__, __TIME__);
            emit_info_kv("Build:", val);
            break;

#if ZUZU_BANNER_SHOW_ADDR
        case 9:
            snprintf(val, sizeof(val), "%P - %P", _kernel_start, _kernel_end);
            emit_info_kv("Kernel:", val);
            break;
        case 10:
            snprintf(val, sizeof(val), "%P - %P",
                    (void*)kernel_layout.heap_start_va, (void*)kernel_layout.heap_end_va);
            emit_info_kv("Heap:", val);
            break;
        case 11:
            snprintf(val, sizeof(val), "%P - %P",
                    (void*)kernel_layout.stack_base_va, (void*)kernel_layout.stack_top_va);
            emit_info_kv("Stack:", val);
            break;
        case 12:
            snprintf(val, sizeof(val), "%u free / %u pages",
                    (unsigned)pmm_state.free_pages, (unsigned)pmm_state.total_pages);
            emit_info_kv("PMM:", val);
            break;
        case 14:
#else
        case 9:
            snprintf(val, sizeof(val), "%u free / %u pages",
                    (unsigned)pmm_state.free_pages, (unsigned)pmm_state.total_pages);
            emit_info_kv("PMM:", val);
            break;
        case 11:
#endif
            emit_tiles();
            break;

        default:
            break;
    }
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

    const int lines = (int)(sizeof(logo) / sizeof(logo[0]));

    for (int i = 0; i < lines; i++) {
        emit_logo_line(logo[i]);
        kprintf("  ");
        emit_info_line(i);
        kprintf("\n");
    }

    // Ensure we reset terminal attributes after the banner
    kprintf("%s", ANSI_RESET);
}
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

// Returns length up to NUL (ASCII). Logo is spaces + blocks; still counts bytes.
static int cstr_len(const char *s)
{
    int n = 0;
    while (s && s[n]) n++;
    return n;
}

// Emit a logo line with coloring. Does not emit newline.
static void emit_logo_line(const char *line)
{
    int nonspace = 0;
    for (const char *p = line; *p; p++) {
        if (*p != ' ') { nonspace = 1; break; }
    }

    const char *logo_col = nonspace ? ANSI_CYAN : (ANSI_BLUE);
    kprintf("%s%s%s", logo_col, line, ANSI_RESET);
}

static void emit_tiles(void)
{
    // "Tiles:" label included in info row
    kprintf("%sTiles:%s    ", ANSI_BLUE, ANSI_RESET);

    // 8 tiles (2 spaces each)
    kprintf("\033[40m  \033[0m");
    kprintf("\033[41m  \033[0m");
    kprintf("\033[42m  \033[0m");
    kprintf("\033[43m  \033[0m");
    kprintf("\033[44m  \033[0m");
    kprintf("\033[45m  \033[0m");
    kprintf("\033[46m  \033[0m");
    kprintf("\033[47m  \033[0m");
}

// To make boxing easy WITHOUT snprintf, we print info content,
// and we also return an *approximate* visible width so we can pad.
// For ANSI-colored strings, visible width != byte count; we just track it manually.
static int emit_info_line(int i)
{
    // Return visible chars printed (not counting ANSI escape sequences).
    // Keep these numbers aligned with the literals.

    switch (i) {
        case 0:
        case 1:
            return 0;

        case 2: {
            // "     zuzu " + version
            // visible width: 5 + 5 + 1 + len(version) = 11 + len
            const int vlen = cstr_len(ZUZU_VERSION);
            kprintf("%s           zuzu %s%s", ANSI_CYAN, ZUZU_VERSION, ANSI_RESET);
            return 11 + vlen;
        }

        case 3: {
            char machine[64] = "Unknown";
            (void)dtb_get_string("/", "model", machine, sizeof(machine));
            // "Machine:  " = 10 visible chars (7+2+1?), actually:
            // "Machine:"(8) + two spaces (2) = 10, plus model len
            kprintf("%sMachine:%s  %s", ANSI_BLUE, ANSI_RESET, machine);
            return 10 + cstr_len(machine);
        }

        case 4:
            kprintf("%sCPU:%s      ARM Cortex-A15", ANSI_BLUE, ANSI_RESET);
            // "CPU:" 4 + 6 spaces = 10 + "ARM Cortex-A15" 13 => 23
            return 4 + 6 + 13;

        case 5: {
            uint32_t ram_mb  = (uint32_t)((pmm_state.total_pages * (uint64_t)PAGE_SIZE) / 1024 / 1024);
            uint32_t free_mb = (uint32_t)((pmm_state.free_pages  * (uint64_t)PAGE_SIZE) / 1024 / 1024);
            kprintf("%sMemory:%s   %u MB free / %u MB total", ANSI_BLUE, ANSI_RESET, free_mb, ram_mb);
            //kprintf("%sPages:%s   %u MB free / %u MB total", ANSI_BLUE, ANSI_RESET, pmm_state.free_pages, pmm_state.total_pages);
            // Hard to count digits without snprintf; just return a conservative estimate.
            return 32; // used only for padding; ok if slightly off
        }

        case 6:
            kprintf("%sTimer:%s    %u Hz", ANSI_BLUE, ANSI_RESET, get_tick_rate());
            return 14; // estimate

        case 7:
            kprintf("%sBuild:%s    %s %s", ANSI_BLUE, ANSI_RESET, __DATE__, __TIME__);
            return 6 + 4 + 1 + 8; // estimate

        case 8:
            emit_tiles();
            return 6 + 4 + (8 * 2); // "Tiles:" + spaces + 16 visible tile chars

#if ZUZU_BANNER_SHOW_ADDR
        case 10:
            kprintf("%sKernel:%s   %P - %P", ANSI_BLUE, ANSI_RESET, _kernel_start, _kernel_end);
            return 32; // estimate
        case 11:
            kprintf("%sHeap:%s     %P - %P", ANSI_BLUE, ANSI_RESET,
                    (void*)kernel_layout.heap_start_va, (void*)kernel_layout.heap_end_va);
            return 32;
        case 12:
            kprintf("%sStack:%s    %P - %P", ANSI_BLUE, ANSI_RESET,
                    (void*)kernel_layout.stack_base_va, (void*)kernel_layout.stack_top_va);
            return 32;
#else
        case 10:
            kprintf("%sPMM:%s      %u free / %u pages", ANSI_BLUE, ANSI_RESET,
                    (unsigned)pmm_state.free_pages, (unsigned)pmm_state.total_pages);
            return 28; // estimate
#endif
        default:
            return 0;
    }
}

void print_boot_banner(void)
{
    static const char *logo[] = {
        "                                         ",
        "                                         ",
        "       ████████                          ",
        "           ██             ████   ██      ",
        "         ███              ██     ██      ",
        "       ███               ███    ██       ",
        "      ██                 ██     ██       ",
        "    ███████████████████  ██    ██        ",
        "                         ███████         ",
        "          ██                             ",
        "          ██                 ██          ",
        "          ██    ██████████   ██          ",
        "          ██   ██       ██   ██          ",
        "          ██   ██       ██   ██          ",
        "          ██  ██        ███  ██          ",
        "           ████           ███            ",
        "                                         ",
    };

    const int lines = (int)(sizeof(logo) / sizeof(logo[0]));

    for (int i = 0; i < lines; i++) {
        emit_logo_line(logo[i]);
        kprintf("  ");
        (void)emit_info_line(i);
        kprintf("\n");
    }

    // Ensure we reset terminal attributes after the banner
    kprintf("%s", ANSI_RESET);
}
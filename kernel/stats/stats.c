#ifdef STATS_MODE

#include "kernel/stats/stats.h"
#include "drivers/uart/uart.h"
#include "kernel/mm/pmm.h"
#include "kernel/mm/alloc.h"
#include "kernel/proc/process.h"
#include "kernel/sched/sched.h"
#include "kernel/time/tick.h"
#include "kernel/layout.h"
#include "lib/snprintf.h"

#include <stdint.h>
#include <stddef.h>

extern process_t *current_process;
extern pmm_state_t pmm_state;
extern kernel_layout_t kernel_layout;

volatile bool stats_mode_active = false;

#define STATS_BOX_WIDTH 76
#define STATS_REFRESH_TICKS TICK_HZ  /* 1 second */
#define STATS_READY_SNAPSHOT_MAX 6

// ANSI codes (same as panic screen)
#define C_GOLD    "\033[33m"
#define C_WHITE   "\033[97m"
#define C_GREY    "\033[37m"
#define C_DIM     "\033[90m"
#define C_RESET   "\033[0m"
#define ANSI_CLEAR "\033[2J\033[3J\033[H"
#define ANSI_HOME  "\033[H"

static uint64_t last_render_tick;

// ── Direct UART output helpers (bypass kprintf) ──

static void stats_puts(const char *s)
{
    while (*s)
        uart_putc(*s++);
}

static void stats_pad(int n)
{
    for (int i = 0; i < n; i++)
        uart_putc(' ');
}

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

static void stats_box_rule(void)
{
    stats_puts(C_GREY "+");
    for (int i = 0; i < STATS_BOX_WIDTH - 2; i++)
        uart_putc('-');
    stats_puts("+" C_RESET "\n");
}

static void stats_box_line(const char *content)
{
    int inner = STATS_BOX_WIDTH - 4;
    int vlen = visible_len(content);
    int pad = inner - vlen;
    if (pad < 0) pad = 0;

    stats_puts(C_GREY "| " C_RESET);
    stats_puts(content);
    stats_pad(pad);
    stats_puts(C_GREY " |" C_RESET "\n");
}

static void stats_box_empty(void)
{
    stats_box_line("");
}

static void stats_box_header(const char *title)
{
    char line[128];
    snprintf(line, sizeof(line), C_GOLD "%s" C_RESET, title);
    stats_box_line(line);
}

// ── Logo (same as panic screen) ──

static const char *stats_logo[] = {
    "      " C_WHITE "@@@@@@" C_RESET,
    "        " C_WHITE "%@@" C_RESET "         " C_WHITE "%@@@  @" C_RESET,
    "       " C_WHITE "@@" C_RESET "           " C_WHITE "@@" C_RESET "   " C_WHITE "@@" C_RESET,
    "     " C_WHITE "@@" C_RESET "             " C_WHITE "@" C_RESET "    " C_WHITE "@=" C_RESET,
    "    " C_WHITE "@@@@@@@@@@@@@@" C_RESET " " C_WHITE "@@" C_RESET "   " C_WHITE "@@" C_RESET,
    "        " C_WHITE "@@" C_RESET "          " C_WHITE "@@@@" C_RESET,
    "        " C_WHITE "@@" C_RESET "             " C_WHITE "@" C_RESET,
    "        " C_WHITE "@@  %@@@@@@@  *@" C_RESET,
    "        " C_WHITE "@@  @" C_RESET "      " C_WHITE "@@ @@" C_RESET,
    "        " C_WHITE "@@  @" C_RESET "      " C_WHITE "@@ @@" C_RESET,
    "         " C_WHITE "@@@" C_RESET "        " C_WHITE "@@@" C_RESET,
};

#define STATS_LOGO_LINES (sizeof(stats_logo) / sizeof(stats_logo[0]))
#define STATS_LOGO_WIDTH 30

static void stats_render_logo(void)
{
    int offset = (STATS_BOX_WIDTH - STATS_LOGO_WIDTH) / 2;
    if (offset < 0) offset = 0;

    for (unsigned i = 0; i < STATS_LOGO_LINES; i++) {
        stats_pad(offset);
        stats_puts(stats_logo[i]);
        stats_puts("\n");
    }
}

// ── Process state name ──

static const char *state_name(p_state_t s)
{
    switch (s) {
    case PROCESS_READY:   return "READY";
    case PROCESS_RUNNING: return "RUNNING";
    case PROCESS_BLOCKED: return "BLOCKED";
    case PROCESS_ZOMBIE:  return "ZOMBIE";
    }
    return "?";
}

// ── Render the stats screen ──

static void stats_render(void)
{
    char line[128];
    uint64_t ticks = get_ticks();
    uint32_t uptime_sec = (uint32_t)(ticks / TICK_HZ);
    uint32_t uptime_min = uptime_sec / 60;
    uptime_sec %= 60;

    stats_puts(ANSI_HOME);

    // Logo
    stats_puts("\n");
    stats_render_logo();
    stats_puts("\n");

    stats_box_rule();
    stats_box_header("ZUZU KERNEL STATS");
    stats_box_empty();

    snprintf(line, sizeof(line), "  Uptime: %lum %lus  (%lu ticks @ %luHz)",
             (unsigned long)uptime_min, (unsigned long)uptime_sec,
             (unsigned long)ticks, (unsigned long)TICK_HZ);
    stats_box_line(line);
    stats_box_rule();

    // Memory
    stats_box_header("MEMORY");
    stats_box_empty();

    size_t pmm_free = pmm_state.free_pages;
    size_t pmm_total = pmm_state.total_pages;
    snprintf(line, sizeof(line), "  PMM: %lu / %lu pages free (%lu KB / %lu KB)",
             (unsigned long)pmm_free, (unsigned long)pmm_total,
             (unsigned long)(pmm_free * PAGE_SIZE / 1024),
             (unsigned long)(pmm_total * PAGE_SIZE / 1024));
    stats_box_line(line);

    if (heap_head && kernel_layout.heap_start_va && kernel_layout.heap_end_va) {
        size_t heap_free_bytes = 0;
        size_t heap_free_blocks = 0;
        size_t heap_used_blocks = 0;

        kmem_block_t *block = heap_head;
        size_t seen = 0;
        while (block && seen < 8192) {
            if (block->free) {
                heap_free_bytes += block->size;
                heap_free_blocks++;
            } else {
                heap_used_blocks++;
            }
            block = block->next;
            seen++;
        }

        size_t heap_total =
            (size_t)((uintptr_t)kernel_layout.heap_end_va -
                     (uintptr_t)kernel_layout.heap_start_va);

        snprintf(line, sizeof(line), "  Heap: %lu / %lu bytes free",
                 (unsigned long)heap_free_bytes, (unsigned long)heap_total);
        stats_box_line(line);
        snprintf(line, sizeof(line), "  Blocks: %lu free, %lu used",
                 (unsigned long)heap_free_blocks, (unsigned long)heap_used_blocks);
        stats_box_line(line);
    } else {
        stats_box_line("  Heap: unavailable");
    }
    stats_box_rule();

    // Current process
    stats_box_header("CURRENT PROCESS");
    stats_box_empty();
    if (current_process) {
        snprintf(line, sizeof(line), "  pid=%-4u ppid=%-4u %s",
                 current_process->pid, current_process->parent_pid,
                 state_name(current_process->process_state));
        stats_box_line(line);
        snprintf(line, sizeof(line), "  prio=%u  slice=%u  left=%u",
                 current_process->priority,
                 current_process->time_slice,
                 current_process->ticks_remaining);
        stats_box_line(line);
    } else {
        stats_box_line("  (idle)");
    }
    stats_box_rule();

    // Ready queue
    stats_box_header("READY QUEUE");
    stats_box_empty();

    process_t *ready[STATS_READY_SNAPSHOT_MAX];
    size_t ready_total = sched_ready_queue_snapshot(ready, STATS_READY_SNAPSHOT_MAX);
    size_t ready_shown = (ready_total < STATS_READY_SNAPSHOT_MAX)
                             ? ready_total
                             : STATS_READY_SNAPSHOT_MAX;

    snprintf(line, sizeof(line), "  Total: %lu  (showing %lu)",
             (unsigned long)ready_total, (unsigned long)ready_shown);
    stats_box_line(line);
    stats_box_empty();

    for (size_t i = 0; i < ready_shown; i++) {
        snprintf(line, sizeof(line), "  #%lu  pid=%-4u %s  prio=%u",
                 (unsigned long)i,
                 ready[i]->pid,
                 state_name(ready[i]->process_state),
                 ready[i]->priority);
        stats_box_line(line);
    }
    if (ready_total == 0) {
        stats_box_line("  (empty)");
    }
    stats_box_rule();

    // Footer
    stats_puts("\n");
    stats_puts(C_DIM "  Press [Enter] to toggle stats mode" C_RESET "\n");
}

// ── Enter / exit ──

static void stats_enter(void)
{
    stats_mode_active = true;
    last_render_tick = get_ticks();
    stats_puts(ANSI_CLEAR);
    stats_render();
}

static void stats_exit(void)
{
    stats_mode_active = false;
    stats_puts(ANSI_CLEAR);
}

// ── Input polling (called from tick_announce) ──

void stats_check_input(void)
{
    const struct uart_driver *drv = uart_get_driver();
    if (!drv || !drv->getc) return;

    int c;
    while ((c = drv->getc()) >= 0) {
        if (c == '\r' || c == '\n') {
            if (stats_mode_active)
                stats_exit();
            else
                stats_enter();
        }
    }

    // Periodic refresh while active
    if (stats_mode_active) {
        uint64_t now = get_ticks();
        if (now - last_render_tick >= STATS_REFRESH_TICKS) {
            last_render_tick = now;
            stats_render();
        }
    }
}

#endif /* STATS_MODE */

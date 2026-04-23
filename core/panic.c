// panic.c - Kernel panic handling and panic screen rendering

#include "core/panic.h"
#include "core/kprintf.h"
#include "arch/arm/include/irq.h"
#include "arch/arm/include/symbols.h"
#include "drivers/uart/uart.h"
#include "kernel/layout.h"
#include "kernel/mm/alloc.h"
#include "kernel/mm/pmm.h"
#include "kernel/proc/process.h"
#include "kernel/sched/sched.h"
#include <string.h>
#include <snprintf.h>
#include <stdint.h>

extern kernel_layout_t kernel_layout;
extern process_t      *current_process;
extern pmm_state_t     pmm_state;

panic_fault_context_t panic_fault_ctx;

#define BACKTRACE_MAX_DEPTH       16
#define PANIC_BOX_WIDTH           76  /* total box width including borders */
#define PANIC_COL_WIDTH           38  /* each column half-width             */
#define PANIC_COL_MAX_LINES       24
#define PANIC_READY_SNAPSHOT_MAX   4
#define PANIC_BT_SHOWN_MAX         8

/* Colors — text only, no background */
#define C_GOLD    "\033[33m"
#define C_WHITE   "\033[97m"
#define C_GREY    "\033[37m"
#define C_DIM     "\033[90m"
#define C_RESET   "\033[0m"
#define ANSI_CLEAR "\033[2J\033[3J\033[H"

static void panic_puts(const char *s)
{
    while (*s)
        uart_putc(*s++);
}

/* ============================================================
 * Full-screen rendering helpers
 * ============================================================ */
#ifdef PANIC_FULL_SCREEN

static void panic_pad(int n)
{
    for (int i = 0; i < n; i++)
        uart_putc(' ');
}

/*
 * Print one line inside the full-width box:
 *   "| <content><padding> |"
 * visible_len() skips ANSI escape sequences when measuring.
 */
static void panic_box_line(const char *content)
{
    int inner = PANIC_BOX_WIDTH - 4;
    int vlen  = visible_len(content);
    int pad   = inner - vlen;
    if (pad < 0) pad = 0;

    panic_puts(C_GREY "| " C_RESET);
    panic_puts(content);
    panic_pad(pad);
    panic_puts(C_GREY " |" C_RESET "\n");
}

/* Full-width horizontal rule: "+----...----+" */
static void panic_box_rule(void)
{
    panic_puts(C_GREY "+");
    for (int i = 0; i < PANIC_BOX_WIDTH - 2; i++)
        uart_putc('-');
    panic_puts("+" C_RESET "\n");
}

/* ============================================================
 * Column-based side-by-side rendering
 * ============================================================ */

typedef struct {
    char lines[PANIC_COL_MAX_LINES][96];
    int  count;
} panic_col_t;

static panic_col_t _col_left, _col_right;

static int _col_append(char *dst, int pos, int max, const char *s)
{
    while (*s && pos < max - 1)
        dst[pos++] = *s++;
    return pos;
}

static void col_init(panic_col_t *c) { c->count = 0; }

static void col_line(panic_col_t *c, const char *content)
{
    if (c->count >= PANIC_COL_MAX_LINES) return;
    char *d = c->lines[c->count];
    int   p = 0;
    p = _col_append(d, p, 96, C_GREY "| " C_RESET);
    p = _col_append(d, p, 96, content);
    int pad = (PANIC_COL_WIDTH - 4) - visible_len(content);
    if (pad < 0) pad = 0;
    for (int i = 0; i < pad && p < 93; i++) d[p++] = ' ';
    p = _col_append(d, p, 96, C_GREY " |" C_RESET);
    d[p] = '\0';
    c->count++;
}

static void col_rule(panic_col_t *c)
{
    if (c->count >= PANIC_COL_MAX_LINES) return;
    char *d = c->lines[c->count];
    int   p = 0;
    p = _col_append(d, p, 96, C_GREY);
    d[p++] = '+';
    for (int i = 0; i < PANIC_COL_WIDTH - 2; i++) d[p++] = '-';
    d[p++] = '+';
    p = _col_append(d, p, 96, C_RESET);
    d[p] = '\0';
    c->count++;
}

static void col_header(panic_col_t *c, const char *title)
{
    char tmp[64];
    snprintf(tmp, sizeof(tmp), C_GOLD "%s" C_RESET, title);
    col_line(c, tmp);
}

/*
 * Print left and right column lines side-by-side.
 * Pads with spaces if one column is shorter than the other.
 * Each pair of lines totals PANIC_BOX_WIDTH (38+38) characters.
 */
static void panic_render_pair(panic_col_t *left, panic_col_t *right)
{
    int max = left->count > right->count ? left->count : right->count;
    for (int i = 0; i < max; i++) {
        if (i < left->count) {
            panic_puts(left->lines[i]);
        } else {
            panic_puts(C_GREY "| " C_RESET);
            panic_pad(PANIC_COL_WIDTH - 4);
            panic_puts(C_GREY " |" C_RESET);
        }

        if (i < right->count) {
            panic_puts(right->lines[i]);
        } else {
            panic_puts(C_GREY "| " C_RESET);
            panic_pad(PANIC_COL_WIDTH - 4);
            panic_puts(C_GREY " |" C_RESET);
        }

        panic_puts("\n");
    }
}

#endif /* PANIC_FULL_SCREEN (rendering helpers) */

/* ============================================================
 * Backtrace walker
 * ============================================================ */

typedef struct {
    uint32_t addresses[BACKTRACE_MAX_DEPTH];
    int      depth;
} backtrace_t;

typedef struct {
    size_t free_payload_bytes;
    size_t used_payload_bytes;
    size_t total_payload_bytes;
    size_t block_count;
    size_t free_blocks;
    size_t used_blocks;
} panic_heap_stats_t;

static void backtrace_walk(backtrace_t *bt)
{
    bt->depth = 0;

    uint32_t fp;
    __asm__ volatile("mov %0, r11" : "=r"(fp));

    uintptr_t stack_lo = (uintptr_t)__svc_stack_base__;
    uintptr_t stack_hi = (uintptr_t)__svc_stack_top__;

    if (kernel_layout.stack_base_va != 0 && kernel_layout.stack_top_va != 0) {
        stack_lo = kernel_layout.stack_base_va;
        stack_hi = kernel_layout.stack_top_va;
    }

    while (bt->depth < BACKTRACE_MAX_DEPTH) {
        if (fp == 0 || (fp & 0x3) || fp < stack_lo || fp >= stack_hi)
            break;
        /* GCC ARM AAPCS prologue: [fp] = saved lr, [fp-4] = saved fp */
        uint32_t lr      = *(uint32_t *)(fp);
        bt->addresses[bt->depth++] = lr;
        uint32_t prev_fp = *(uint32_t *)(fp - 4);
        if (prev_fp <= fp)
            break;
        fp = prev_fp;
    }
}

static void panic_heap_snapshot(panic_heap_stats_t *st)
{
    memset(st, 0, sizeof(*st));
    kmem_block_t *block = heap_head;
    size_t seen = 0;
    while (block && seen < 8192) {
        st->block_count++;
        st->total_payload_bytes += block->size;
        if (block->free) {
            st->free_payload_bytes += block->size;
            st->free_blocks++;
        } else {
            st->used_payload_bytes += block->size;
            st->used_blocks++;
        }
        block = block->next;
        seen++;
    }
}

/* ============================================================
 * Logo (unchanged)
 * ============================================================ */

#ifdef PANIC_FULL_SCREEN
static const char *panic_logo[] = {
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

#define PANIC_LOGO_LINES (sizeof(panic_logo) / sizeof(panic_logo[0]))
#define PANIC_LOGO_WIDTH 30  /* approximate visible width */
#endif

/* ============================================================
 * Shared helpers
 * ============================================================ */

static const char *panic_proc_state_str(int state)
{
    switch (state) {
    case PROCESS_READY:   return "READY";
    case PROCESS_RUNNING: return "RUNNING";
    case PROCESS_BLOCKED: return "BLOCKED";
    case PROCESS_ZOMBIE:  return "ZOMBIE";
    case PROCESS_STOPPED: return "STOPPED";
    default:              return "UNKNOWN";
    }
}

/* ============================================================
 * Panic screens
 * ============================================================ */

/*
 * FULL-SCREEN layout  (PANIC_FULL_SCREEN != 0)
 * ---------------------------------------------
 *
 * [logo + banner — unchanged]
 *
 * +------------------------------------------------------------------------+  ← top
 * | KERNEL PANIC  reason: <reason>                                        |
 * | caller: 0xXXXXXXXX                                                    |
 * +------------------------------------------------------------------------+  ← (if fault) divider
 * | FAULT  type: xxx  fault: xxx  access: xxx                             |
 * |        far:  0xXXXXXXXX  fsr: 0xXXXXXXXX                             |
 * +--------------------------------------++---------------------------------+  ← col-rule pair*
 * | REGISTERS                            || BACKTRACE                      |
 * |   r0=XXXXXXXX  r1=XXXXXXXX           ||   #0  0xXXXXXXXX              |
 * |   ...                                ||   addr2line -e build/zuzu.elf  |
 * +------------------------------------------------------------------------+  ← footer divider
 * | PROCESS  pid=x ppid=x RUNNING  prio=x  slice=x  left=x               |
 * |          ptr=0xXXXX  as=0xXXXX  ready=x  pids: 1 2 3                 |
 * | MEMORY   pmm: x/y pages free                                          |
 * |          heap: free=x used=x  total=x  blks=x                        |
 * +------------------------------------------------------------------------+  ← bottom
 *
 * * col-rule pair serves as both fault-close and register/backtrace open,
 *   eliminating a redundant consecutive rule.
 *
 * When no fault frame is available (backtrace-only):
 *   • Backtrace runs full-width, one entry per line.
 *   • If panic_fault_ctx.frame is set without full fault context,
 *     registers are printed 4-per-line above the backtrace.
 */

#if PANIC_FULL_SCREEN

static void panic_screen(const char *reason, void *caller_ra)
{
    char line[128];

    uart_putc('\a');
    panic_puts(ANSI_CLEAR);
    panic_puts("\n");

    /* -- Logo left, intro text right ------------------------------------ */
    const char *banner_lines[] = {
        C_WHITE "Oops! zuzu has panicked." C_RESET,
        C_DIM "An unrecoverable kernel error occurred." C_RESET,
        C_DIM "System halted to preserve state." C_RESET,
        C_DIM "https://github.com/kagantmr/zuzu/issues" C_RESET,
    };
    const int banner_count = (int)(sizeof(banner_lines) / sizeof(banner_lines[0]));
    const int logo_left_pad = 2;
    const int text_col = logo_left_pad + PANIC_LOGO_WIDTH + 2;

    for (int i = 0; i < (int)PANIC_LOGO_LINES; i++) {
        panic_pad(logo_left_pad);
        panic_puts(panic_logo[i]);

        int used = logo_left_pad + visible_len(panic_logo[i]);
        int gap = text_col - used;
        if (gap < 1) gap = 1;
        panic_pad(gap);

        if (i < banner_count)
            panic_puts(banner_lines[i]);

        panic_puts("\n");
    }
    panic_puts("\n");

    /* KERNEL PANIC header */
    panic_box_rule();

    snprintf(line, sizeof(line),
             C_GOLD "KERNEL PANIC" C_RESET "  reason: %s",
             reason ? reason : "unknown");
    panic_box_line(line);

    snprintf(line, sizeof(line), "  caller: %p", caller_ra);
    panic_box_line(line);

    /* -- Fault details (inline — no extra box, just a divider + lines) -- */
    if (panic_fault_ctx.valid) {
        panic_box_rule();

        /* Pack type / decoded / access onto a single line */
        int pos = snprintf(line, sizeof(line), C_GOLD "FAULT" C_RESET);
        if (panic_fault_ctx.fault_type)
            pos += snprintf(line + pos, sizeof(line) - (size_t)pos,
                            "  type: %s", panic_fault_ctx.fault_type);
        if (panic_fault_ctx.fault_decoded)
            pos += snprintf(line + pos, sizeof(line) - (size_t)pos,
                            "  fault: %s", panic_fault_ctx.fault_decoded);
        if (panic_fault_ctx.access_type)
            pos += snprintf(line + pos, sizeof(line) - (size_t)pos,
                            "  access: %s", panic_fault_ctx.access_type);
        panic_box_line(line);

        if (panic_fault_ctx.far || panic_fault_ctx.fsr) {
            snprintf(line, sizeof(line),
                     "       far:  0x%08X   fsr: 0x%08X",
                     panic_fault_ctx.far, panic_fault_ctx.fsr);
            panic_box_line(line);
        }
    }

    /* -- Walk backtrace once, before choosing layout -------------------- */
    backtrace_t bt;
    backtrace_walk(&bt);

    /* -- REGISTERS | BACKTRACE (side-by-side columns) ------------------ */
    if (panic_fault_ctx.valid && panic_fault_ctx.frame) {
        exception_frame_t *f = panic_fault_ctx.frame;

        /*
         * Left column: REGISTERS (2/line — fits col width comfortably)
         * col_rule at the top only; panic_box_rule() closes the section
         * at the footer, so no redundant trailing col_rule here.
         */
        col_init(&_col_left);
        col_rule(&_col_left);
        col_header(&_col_left, "REGISTERS");
        snprintf(line, sizeof(line), "  r0=%08X  r1=%08X", f->r[0],  f->r[1]);  col_line(&_col_left, line);
        snprintf(line, sizeof(line), "  r2=%08X  r3=%08X", f->r[2],  f->r[3]);  col_line(&_col_left, line);
        snprintf(line, sizeof(line), "  r4=%08X  r5=%08X", f->r[4],  f->r[5]);  col_line(&_col_left, line);
        snprintf(line, sizeof(line), "  r6=%08X  r7=%08X", f->r[6],  f->r[7]);  col_line(&_col_left, line);
        snprintf(line, sizeof(line), "  r8=%08X  r9=%08X", f->r[8],  f->r[9]);  col_line(&_col_left, line);
        snprintf(line, sizeof(line), "  rA=%08X  rB=%08X", f->r[10], f->r[11]); col_line(&_col_left, line);
        snprintf(line, sizeof(line), "  rC=%08X  lr=%08X", f->r[12], f->lr_usr);col_line(&_col_left, line);
        snprintf(line, sizeof(line), "  pc=%08X  cpsr=%08X",
                 f->return_pc, f->return_cpsr);
        col_line(&_col_left, line);

        /* Right column: BACKTRACE */
        col_init(&_col_right);
        col_rule(&_col_right);
        col_header(&_col_right, "BACKTRACE");
        if (bt.depth == 0) {
            col_line(&_col_right, "  (no frames)");
        } else {
            int shown = (bt.depth < PANIC_BT_SHOWN_MAX) ? bt.depth : PANIC_BT_SHOWN_MAX;
            for (int i = 0; i < shown; i++) {
                snprintf(line, sizeof(line), "  #%-2d 0x%08X", i, bt.addresses[i]);
                col_line(&_col_right, line);
            }
            if (bt.depth > shown) {
                snprintf(line, sizeof(line), "  ... +%d more", bt.depth - shown);
                col_line(&_col_right, line);
            }
        }
        snprintf(line, sizeof(line), C_DIM "  addr2line -e build/zuzu.elf" C_RESET);
        col_line(&_col_right, line);

        /* col_rule pair acts as the visual divider from the fault section */
        panic_render_pair(&_col_left, &_col_right);

    } else {
        /*
         * No fault frame, full-width layout.
         *
         * If panic_fault_ctx.frame is set without a complete fault context
         * (e.g. a software-initiated panic after a partial exception setup),
         * display registers 4-per-line to make better use of the width.
         */
        panic_box_rule();

        if (panic_fault_ctx.frame) {
            exception_frame_t *f = panic_fault_ctx.frame;
            panic_box_line(C_GOLD "REGISTERS" C_RESET);
            snprintf(line, sizeof(line),
                     "  r0=%08X  r1=%08X  r2=%08X  r3=%08X",
                     f->r[0], f->r[1], f->r[2], f->r[3]);
            panic_box_line(line);
            snprintf(line, sizeof(line),
                     "  r4=%08X  r5=%08X  r6=%08X  r7=%08X",
                     f->r[4], f->r[5], f->r[6], f->r[7]);
            panic_box_line(line);
            snprintf(line, sizeof(line),
                     "  r8=%08X  r9=%08X  rA=%08X  rB=%08X",
                     f->r[8], f->r[9], f->r[10], f->r[11]);
            panic_box_line(line);
            snprintf(line, sizeof(line),
                     "  rC=%08X  lr=%08X  pc=%08X  cpsr=%08X",
                     f->r[12], f->lr_usr, f->return_pc, f->return_cpsr);
            panic_box_line(line);
        }

        panic_box_line(C_GOLD "BACKTRACE" C_RESET);
        if (bt.depth == 0) {
            panic_box_line("  (no frames)");
        } else {
            for (int i = 0; i < bt.depth; i++) {
                snprintf(line, sizeof(line), "  #%-2d  0x%08X", i, bt.addresses[i]);
                panic_box_line(line);
            }
        }
        snprintf(line, sizeof(line),
                 C_DIM "  addr2line -e build/zuzu.elf <addr>" C_RESET);
        panic_box_line(line);
    }

    /* -- PROCESS + MEMORY footer ---------------------------------------- */
    /*
     * Single panic_box_rule() closes the register/backtrace section and
     * opens the footer — no duplicate consecutive rules.
     */
    panic_box_rule();

    /* PROCESS */
    if (current_process) {
        snprintf(line, sizeof(line),
                 C_GOLD "PROCESS" C_RESET
                 "  pid=%-4u ppid=%-4u %-7s  prio=%u  slice=%u  left=%u",
                 current_process->pid,
                 current_process->parent_pid,
                 panic_proc_state_str(current_process->process_state),
                 current_process->priority,
                 current_process->time_slice,
                 current_process->ticks_remaining);
        panic_box_line(line);

        snprintf(line, sizeof(line), "         ptr=%p  as=%p",
                 current_process, current_process->as);
        panic_box_line(line);

        process_t *ready[PANIC_READY_SNAPSHOT_MAX];
        size_t ready_total = sched_ready_queue_snapshot(ready, PANIC_READY_SNAPSHOT_MAX);
        size_t ready_shown = (ready_total < PANIC_READY_SNAPSHOT_MAX)
                             ? ready_total : PANIC_READY_SNAPSHOT_MAX;
        if (ready_total > 0) {
            int pos = snprintf(line, sizeof(line),
                               "         ready=%lu  pids:",
                               (unsigned long)ready_total);
            for (size_t i = 0; i < ready_shown && pos > 0; i++)
                pos += snprintf(line + pos, sizeof(line) - (size_t)pos,
                                " %u", ready[i]->pid);
            if (ready_total > ready_shown)
                pos += snprintf(line + pos, sizeof(line) - (size_t)pos,
                                " (+%lu)", (unsigned long)(ready_total - ready_shown));
            panic_box_line(line);
        }
    } else {
        panic_box_line(C_GOLD "PROCESS" C_RESET "  (none)");
    }

    /* MEMORY */
    {
        snprintf(line, sizeof(line),
                 C_GOLD "MEMORY " C_RESET "  pmm: %lu/%lu pages free",
                 (unsigned long)pmm_state.free_pages,
                 (unsigned long)pmm_state.total_pages);
        panic_box_line(line);

        if (heap_head && kernel_layout.heap_start_va && kernel_layout.heap_end_va) {
            panic_heap_stats_t hs;
            panic_heap_snapshot(&hs);
            snprintf(line, sizeof(line),
                     "         heap: free=%lu  used=%lu  total=%lu  blks=%lu",
                     (unsigned long)hs.free_payload_bytes,
                     (unsigned long)hs.used_payload_bytes,
                     (unsigned long)hs.total_payload_bytes,
                     (unsigned long)hs.block_count);
            panic_box_line(line);
        } else {
            panic_box_line("         heap: unavailable");
        }
    }

    panic_box_rule();
    panic_puts("\n");
}

#else  

static void panic_screen(const char *reason, void *caller_ra)
{
    uart_putc('\a');

    kprintf("\n\033[1;31m--- KERNEL PANIC ---\033[0m\n");
    kprintf("  reason:  %s\n", reason ? reason : "unknown");
    kprintf("  caller:  %p\n", caller_ra);

    /* Fault details */
    if (panic_fault_ctx.valid) {
        kprintf("  ---\n");
        if (panic_fault_ctx.fault_type)
            kprintf("  type:    %s\n", panic_fault_ctx.fault_type);
        if (panic_fault_ctx.fault_decoded)
            kprintf("  fault:   %s\n", panic_fault_ctx.fault_decoded);
        if (panic_fault_ctx.access_type)
            kprintf("  access:  %s\n", panic_fault_ctx.access_type);
        if (panic_fault_ctx.far || panic_fault_ctx.fsr)
            kprintf("  far:     0x%08X   fsr: 0x%08X\n",
                    panic_fault_ctx.far, panic_fault_ctx.fsr);

        if (panic_fault_ctx.frame) {
            exception_frame_t *f = panic_fault_ctx.frame;
            kprintf("  ---\n");
            kprintf("  r0=%08X  r1=%08X  r2=%08X  r3=%08X\n",
                    f->r[0], f->r[1], f->r[2], f->r[3]);
            kprintf("  r4=%08X  r5=%08X  r6=%08X  r7=%08X\n",
                    f->r[4], f->r[5], f->r[6], f->r[7]);
            kprintf("  r8=%08X  r9=%08X  rA=%08X  rB=%08X\n",
                    f->r[8], f->r[9], f->r[10], f->r[11]);
            kprintf("  rC=%08X  lr=%08X  pc=%08X  cpsr=%08X\n",
                    f->r[12], f->lr_usr, f->return_pc, f->return_cpsr);
        }
    }

    /* Backtrace */
    backtrace_t bt;
    backtrace_walk(&bt);
    kprintf("  ---\n");
    if (bt.depth == 0) {
        kprintf("  backtrace: (no frames)\n");
    } else {
        kprintf("  backtrace:\n");
        for (int i = 0; i < bt.depth; i++)
            kprintf("    #%-2d  0x%08X\n", i, bt.addresses[i]);
    }

    /* Process */
    kprintf("  ---\n");
    if (current_process) {
        kprintf("  process: pid=%u ppid=%u %s  prio=%u  slice=%u  left=%u\n",
                current_process->pid,
                current_process->parent_pid,
                panic_proc_state_str(current_process->process_state),
                current_process->priority,
                current_process->time_slice,
                current_process->ticks_remaining);
    } else {
        kprintf("  process: (none)\n");
    }

    /* Memory */
    kprintf("  memory:  pmm: %lu/%lu pages free",
            (unsigned long)pmm_state.free_pages,
            (unsigned long)pmm_state.total_pages);
    if (heap_head && kernel_layout.heap_start_va) {
        panic_heap_stats_t hs;
        panic_heap_snapshot(&hs);
        kprintf("  heap: free=%lu  used=%lu  blks=%lu\n",
                (unsigned long)hs.free_payload_bytes,
                (unsigned long)hs.used_payload_bytes,
                (unsigned long)hs.block_count);
    } else {
        kprintf("  heap: unavailable\n");
    }

    kprintf("\033[1;31m--- HALTED ---\033[0m\n\n");
}

#endif /* PANIC_FULL_SCREEN */

/* ============================================================
 * Panic entry point (unchanged)
 * ============================================================ */

_Noreturn void panic(const char *reason)
{
    panic_puts("\a");
    void *caller_ra = __builtin_return_address(0);

    arch_global_irq_disable();

    panic_screen(reason, caller_ra);

    __asm__ volatile(
        "1:\n"
        "    wfi\n"
        "    b 1b\n");
    __builtin_unreachable();
}
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
#include "lib/snprintf.h"

#include <stdint.h>

extern kernel_layout_t kernel_layout;
extern process_t *current_process;
extern pmm_state_t pmm_state;

panic_fault_context_t panic_fault_ctx;

#ifndef PANIC_FULL_SCREEN
#define PANIC_FULL_SCREEN 1
#endif

#define BACKTRACE_MAX_DEPTH 16
#define PANIC_BOX_WIDTH 52
#define PANIC_READY_SNAPSHOT_MAX 6

// Colors — text only, no background
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

static void panic_pad(int n)
{
    for (int i = 0; i < n; i++)
        uart_putc(' ');
}

static void panic_box_line(const char *content)
{
    int inner = PANIC_BOX_WIDTH - 4;
    int vlen = visible_len(content);
    int pad = inner - vlen;
    if (pad < 0) pad = 0;

    panic_puts(C_GREY "| " C_RESET);
    panic_puts(content);
    panic_pad(pad);
    panic_puts(C_GREY " |" C_RESET "\n");
}

static void panic_box_rule(void)
{
    panic_puts(C_GREY "+");
    for (int i = 0; i < PANIC_BOX_WIDTH - 2; i++)
        uart_putc('-');
    panic_puts("+" C_RESET "\n");
}

static void panic_box_empty(void)
{
    panic_box_line("");
}

static void panic_box_header(const char *title)
{
    char line[128];
    snprintf(line, sizeof(line), C_GOLD "%s" C_RESET, title);
    panic_box_line(line);
}

// Center a string within the box width
static void panic_puts_centered(const char *s)
{
    int vlen = visible_len(s);
    int total_pad = PANIC_BOX_WIDTH - vlen;
    if (total_pad < 0) total_pad = 0;
    int left_pad = total_pad / 2;

    panic_pad(left_pad);
    panic_puts(s);
    panic_puts("\n");
}

// ============================================================
// Backtrace walker
// ============================================================

typedef struct {
    uint32_t addresses[BACKTRACE_MAX_DEPTH];
    int depth;
} backtrace_t;

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
        if (fp == 0)
            break;
        if (fp & 0x3)
            break;
        if (fp < stack_lo || fp >= stack_hi)
            break;

        // GCC ARM prologue: [fp] = saved lr, [fp-4] = saved fp
        uint32_t lr = *(uint32_t *)(fp);
        bt->addresses[bt->depth++] = lr;

        uint32_t prev_fp = *(uint32_t *)(fp - 4);
        if (prev_fp <= fp)
            break;

        fp = prev_fp;
    }
}

// ============================================================
// Panic logo
// ============================================================

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
#define PANIC_LOGO_WIDTH 30  // approximate visible width of logo

// ============================================================
// Panic screens
// ============================================================

#if PANIC_FULL_SCREEN
static void panic_screen(const char *reason, void *caller_ra)
{
    char line[128];

    // Beep! (terminal bell)
    uart_putc('\a');

    panic_puts(ANSI_CLEAR);
    panic_puts("\n");

    int logo_offset = (PANIC_BOX_WIDTH - PANIC_LOGO_WIDTH) / 2;
    if (logo_offset < 0) logo_offset = 0;

    for (unsigned i = 0; i < PANIC_LOGO_LINES; i++) {
        panic_pad(logo_offset);
        panic_puts(panic_logo[i]);
        panic_puts("\n");
    }
    panic_puts("\n");

    panic_puts_centered(C_WHITE "Oops! ZuzuOS has panicked." C_RESET);
    panic_puts_centered(C_DIM "Something went wrong and the system was halted." C_RESET);
    panic_puts_centered(C_DIM "Please share this screen with the developers." C_RESET);
    panic_puts("\n");

    panic_box_rule();
    panic_box_header("KERNEL PANIC");
    panic_box_empty();
    snprintf(line, sizeof(line), "  %s", reason ? reason : "unknown");
    panic_box_line(line);
    panic_box_rule();

    snprintf(line, sizeof(line), "Caller:  %p", caller_ra);
    panic_box_line(line);
    panic_box_rule();

    if (panic_fault_ctx.valid) {
        panic_box_header("FAULT DETAILS");
        panic_box_empty();

        if (panic_fault_ctx.fault_type) {
            snprintf(line, sizeof(line), "  Type:   %s", panic_fault_ctx.fault_type);
            panic_box_line(line);
        }
        if (panic_fault_ctx.fault_decoded) {
            snprintf(line, sizeof(line), "  Fault:  %s", panic_fault_ctx.fault_decoded);
            panic_box_line(line);
        }
        if (panic_fault_ctx.far) {
            snprintf(line, sizeof(line), "  FAR:    0x%08X", panic_fault_ctx.far);
            panic_box_line(line);
        }
        if (panic_fault_ctx.fsr) {
            snprintf(line, sizeof(line), "  FSR:    0x%08X", panic_fault_ctx.fsr);
            panic_box_line(line);
        }
        if (panic_fault_ctx.access_type) {
            snprintf(line, sizeof(line), "  Access: %s", panic_fault_ctx.access_type);
            panic_box_line(line);
        }

        panic_box_rule();

        if (panic_fault_ctx.frame) {
            exception_frame_t *f = panic_fault_ctx.frame;

            panic_box_header("REGISTERS");
            panic_box_empty();

            for (int i = 0; i < 13; i += 2) {
                if (i + 1 < 13)
                    snprintf(line, sizeof(line), "  r%-2d=%08X  r%-2d=%08X",
                             i, f->r[i], i + 1, f->r[i + 1]);
                else
                    snprintf(line, sizeof(line), "  r%-2d=%08X",
                             i, f->r[i]);
                panic_box_line(line);
            }
            snprintf(line, sizeof(line), "  lr =%08X  pc =%08X",
                     f->lr, f->return_pc);
            panic_box_line(line);
            snprintf(line, sizeof(line), "  cpsr=%08X", f->return_cpsr);
            panic_box_line(line);

            panic_box_rule();
        }
    }

    panic_box_header("BACKTRACE");
    panic_box_empty();

    backtrace_t bt;
    backtrace_walk(&bt);

    if (bt.depth == 0) {
        panic_box_line("  (no frames)");
    } else {
        for (int i = 0; i < bt.depth; i++) {
            snprintf(line, sizeof(line), "  #%-2d  0x%08X", i, bt.addresses[i]);
            panic_box_line(line);
        }
    }

    panic_box_empty();
    snprintf(line, sizeof(line), C_DIM "  addr2line -e build/zuzu.elf <addr>" C_RESET);
    panic_box_line(line);
    panic_box_rule();

    panic_box_header("PROCESS");
    if (current_process) {
        const char *state = "UNKNOWN";
        switch (current_process->process_state) {
        case PROCESS_READY:   state = "READY";   break;
        case PROCESS_RUNNING: state = "RUNNING"; break;
        case PROCESS_BLOCKED: state = "BLOCKED"; break;
        case PROCESS_ZOMBIE:  state = "ZOMBIE";  break;
        }

        snprintf(line, sizeof(line), "  ptr=%p  pid=%u  ppid=%u",
                 current_process, current_process->pid, current_process->parent_pid);
        panic_box_line(line);
        snprintf(line, sizeof(line), "  state=%s  as=%p", state, current_process->as);
        panic_box_line(line);
        snprintf(line, sizeof(line), "  prio=%u  slice=%u  left=%u",
                 current_process->priority,
                 current_process->time_slice,
                 current_process->ticks_remaining);
        panic_box_line(line);
    } else {
        panic_box_line("  (none)");
    }
    panic_box_rule();

    panic_box_header("MEMORY");
    size_t pmm_free_pages = pmm_state.free_pages;
    size_t pmm_total_pages = pmm_state.total_pages;
    size_t pmm_used_pages = (pmm_total_pages >= pmm_free_pages) ? (pmm_total_pages - pmm_free_pages) : 0;
    snprintf(line, sizeof(line), "  pmm pages: free=%lu used=%lu total=%lu",
             (unsigned long)pmm_free_pages,
             (unsigned long)pmm_used_pages,
             (unsigned long)pmm_total_pages);
    panic_box_line(line);

    if (heap_head && kernel_layout.heap_start_va && kernel_layout.heap_end_va) {
        size_t heap_free_bytes = 0;
        size_t heap_used_bytes = 0;
        size_t heap_free_blocks = 0;
        size_t heap_used_blocks = 0;

        kmem_block_t *block = heap_head;
        size_t seen = 0;
        while (block && seen < 8192) {
            if (block->free) {
                heap_free_bytes += block->size;
                heap_free_blocks++;
            } else {
                heap_used_bytes += block->size;
                heap_used_blocks++;
            }
            block = block->next;
            seen++;
        }

        size_t heap_total_bytes =
            (size_t)((uintptr_t)kernel_layout.heap_end_va - (uintptr_t)kernel_layout.heap_start_va);

        snprintf(line, sizeof(line), "  heap bytes: free=%lu used=%lu total=%lu",
                 (unsigned long)heap_free_bytes,
                 (unsigned long)heap_used_bytes,
                 (unsigned long)heap_total_bytes);
        panic_box_line(line);

        snprintf(line, sizeof(line), "  heap blocks: free=%lu used=%lu",
                 (unsigned long)heap_free_blocks,
                 (unsigned long)heap_used_blocks);
        panic_box_line(line);
    } else {
        panic_box_line("  heap: unavailable");
    }
    panic_box_rule();

    panic_box_header("READY QUEUE");
    process_t *ready[PANIC_READY_SNAPSHOT_MAX];
    size_t ready_total = sched_ready_queue_snapshot(ready, PANIC_READY_SNAPSHOT_MAX);
    size_t ready_shown = (ready_total < PANIC_READY_SNAPSHOT_MAX) ? ready_total : PANIC_READY_SNAPSHOT_MAX;

    snprintf(line, sizeof(line), "  ready=%lu shown=%lu",
             (unsigned long)ready_total,
             (unsigned long)ready_shown);
    panic_box_line(line);

    if (ready_total == 0) {
        panic_box_line("  (empty)");
    } else {
        for (size_t i = 0; i < ready_shown; i++) {
            const char *state = "UNKNOWN";
            switch (ready[i]->process_state) {
            case PROCESS_READY:   state = "READY";   break;
            case PROCESS_RUNNING: state = "RUNNING"; break;
            case PROCESS_BLOCKED: state = "BLOCKED"; break;
            case PROCESS_ZOMBIE:  state = "ZOMBIE";  break;
            }

            snprintf(line, sizeof(line), "  #%lu pid=%lu st=%s",
                     (unsigned long)i,
                     (unsigned long)ready[i]->pid,
                     state);
            panic_box_line(line);
        }
    }
    panic_box_rule();

    panic_puts("\n");
}
#else
static void panic_screen(const char *reason, void *caller_ra)
{
    uart_putc('\a');

    kprintf("\n\033[1;31m=== KERNEL PANIC: %s ===\033[0m\n",
            reason ? reason : "unknown");
    kprintf("Caller: %p\n", caller_ra);

    backtrace_t bt;
    backtrace_walk(&bt);

    if (panic_fault_ctx.valid) {
        if (panic_fault_ctx.fault_decoded)
            kprintf("Fault: %s\n", panic_fault_ctx.fault_decoded);
        if (panic_fault_ctx.far)
            kprintf("FAR: 0x%08X  FSR: 0x%08X\n", panic_fault_ctx.far, panic_fault_ctx.fsr);
        if (panic_fault_ctx.access_type)
            kprintf("Access: %s\n", panic_fault_ctx.access_type);
        if (panic_fault_ctx.frame) {
            exception_frame_t *f = panic_fault_ctx.frame;
            kprintf("r0=%08X r1=%08X r2=%08X r3=%08X\n", f->r[0], f->r[1], f->r[2], f->r[3]);
            kprintf("r4=%08X r5=%08X r6=%08X r7=%08X\n", f->r[4], f->r[5], f->r[6], f->r[7]);
            kprintf("r8=%08X r9=%08X r10=%08X r11=%08X\n", f->r[8], f->r[9], f->r[10], f->r[11]);
            kprintf("r12=%08X lr=%08X pc=%08X cpsr=%08X\n", f->r[12], f->lr, f->return_pc, f->return_cpsr);
        }
    }

    kprintf("Backtrace:\n");
    for (int i = 0; i < bt.depth; i++)
        kprintf("  #%d  0x%08X\n", i, bt.addresses[i]);

    if (bt.depth == 0)
        kprintf("  (no frames)\n");
}
#endif

_Noreturn void panic(const char *reason)
{
    void *caller_ra = __builtin_return_address(0);

    arch_global_irq_disable();

    panic_screen(reason, caller_ra);

    __asm__ volatile(
        "1:\n"
        "    wfi\n"
        "    b 1b\n");
    __builtin_unreachable();
}

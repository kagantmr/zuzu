// panic.c - Kernel panic screen

#include "core/panic.h"
#include <arch/cpu.h>
#include <arch/irq.h>
#include <arch/symbols.h>
#include BOARD_LAYOUT_H   /* KERNEL_VA_BASE, USER_VA_TOP, IOREMAP_BASE */
#include "drivers/uart/uart.h"
#include "kernel/layout.h"
#include "kernel/mm/alloc.h"
#include "kernel/mm/pmm.h"
#include "kernel/proc/process.h"
#include "kernel/sched/sched.h"
#include "ksym.h"
#include <list.h>
#include <string.h>
#include <snprintf.h>
#include <stdint.h>

#ifdef PANIC_SECTION_IRQ
#include "kernel/irq/sys_irq.h"
extern irq_handler_t      handler_table[MAX_IRQS];
#endif

extern kernel_layout_t kernel_layout;
extern pmm_state_t     pmm_state;
extern list_head_t     sleep_queue;

panic_fault_context_t panic_fault_ctx;

/* ------------------------------------------------------------------ */
/* Constants                                                           */
/* ------------------------------------------------------------------ */

#define BACKTRACE_MAX_DEPTH  16
#define PANIC_READY_MAX       8
#define PANIC_SLEEP_MAX       8
#define PANIC_HANDLE_MAX     16
#define RULE_COL             72
#define LINE_BUF            160

/*
 * Virtual-layout bounds for pointer sanity checks. KERNEL_VA_BASE / USER_VA_TOP
 * come from the board layout; the MMIO window base maps to IOREMAP_BASE.
 */
#ifndef MMIO_VA_BASE
#define MMIO_VA_BASE   IOREMAP_BASE
#endif

/* ------------------------------------------------------------------ */
/* Colors                                                              */
/* ------------------------------------------------------------------ */

#define C_YELLOW "\033[33m"   /* section headers */
#define C_GRAY   "\033[37m"   /* normal content  */
#define C_DIM    "\033[90m"   /* low-emphasis    */
#define C_RED    "\033[91m"   /* errors only     */
#define C_RESET  "\033[0m"

/* Legacy aliases used in the logo */
#define C_AMBER  C_YELLOW
#define C_WHITE  C_GRAY

/* ------------------------------------------------------------------ */
/* Low-level output                                                    */
/* ------------------------------------------------------------------ */

static void panic_puts(const char *s)
{
    while (*s)
        uart_putc(*s++);
}

static void panic_nl(void)
{
    uart_putc('\n');
}

/* Section header: yellow name + dash rule to column RULE_COL */
static void panic_section(const char *name)
{
    panic_puts("\n" C_YELLOW);
    panic_puts(name);
    int n = (int)strlen(name);
    uart_putc(' ');
    for (int i = n + 1; i < RULE_COL; i++)
        uart_putc('-');
    panic_puts(C_RESET "\n");
}

/* 2-space indent, gray content */
static void panic_line(const char *s)
{
    panic_puts("  " C_GRAY);
    panic_puts(s);
    panic_puts(C_RESET "\n");
}

/* ------------------------------------------------------------------ */
/* CPSR / mode decoding                                                */
/* ------------------------------------------------------------------ */

static const char *cpsr_mode_name(uint32_t cpsr)
{
    switch (cpsr & 0x1Fu) {
    case 0x10u: return "USR";
    case 0x11u: return "FIQ";
    case 0x12u: return "IRQ";
    case 0x13u: return "SVC";
    case 0x16u: return "MON";
    case 0x17u: return "ABT";
    case 0x1Au: return "HYP";
    case 0x1Bu: return "UND";
    case 0x1Fu: return "SYS";
    default:    return "???";
    }
}

static void cpsr_decode(char *buf, int bufsz, uint32_t cpsr)
{
    snprintf(buf, (size_t)bufsz,
             "[%s %s irq=%s fiq=%s %c%c%c%c]",
             cpsr_mode_name(cpsr),
             (cpsr & (1u << 5))  ? "Thumb" : "ARM",
             (cpsr & (1u << 7))  ? "dis"   : "en",
             (cpsr & (1u << 6))  ? "dis"   : "en",
             (cpsr >> 31) & 1u   ? 'N'     : 'n',
             (cpsr >> 30) & 1u   ? 'Z'     : 'z',
             (cpsr >> 29) & 1u   ? 'C'     : 'c',
             (cpsr >> 28) & 1u   ? 'V'     : 'v');
}

/* ------------------------------------------------------------------ */
/* FAR range annotation                                                */
/* ------------------------------------------------------------------ */

static const char *far_region(uint32_t addr)
{
    extern char _kernel_start[], _kernel_end[];

    if (addr >= (uint32_t)(uintptr_t)_kernel_start &&
        addr <  (uint32_t)(uintptr_t)_kernel_end)
        return "kernel text/data";

    if (kernel_layout.stack_base_va &&
        addr >= (uint32_t)kernel_layout.stack_base_va &&
        addr <  (uint32_t)kernel_layout.stack_top_va)
        return "kernel stack";

    if (kernel_layout.heap_start_va &&
        addr >= (uint32_t)(uintptr_t)kernel_layout.heap_start_va &&
        addr <  (uint32_t)(uintptr_t)kernel_layout.heap_end_va)
        return "kernel heap";

    if (addr >= MMIO_VA_BASE)
        return "MMIO / high kernel";

    if (addr < USER_VA_TOP)
        return "user space";

    return NULL;
}

/* ------------------------------------------------------------------ */
/* State name helpers                                                  */
/* ------------------------------------------------------------------ */

static const char *thread_state_str(int state)
{
    switch (state) {
    case READY:   return "READY";
    case RUNNING: return "RUNNING";
    case BLOCKED: return "BLOCKED";
    case ZOMBIE:  return "ZOMBIE";
    case FROZEN:  return "FROZEN";
    default:      return "UNKNOWN";
    }
}

static const char *handle_type_str(handle_type_t t)
{
    switch (t) {
    case HANDLE_FREE:         return "FREE";
    case HANDLE_ENDPOINT:     return "ENDPOINT";
    case HANDLE_DEVICE:       return "DEVICE";
    case HANDLE_SHMEM:        return "SHMEM";
    case HANDLE_REPLY:        return "REPLY";
    case HANDLE_NOTIFICATION: return "NOTIFICATION";
    case HANDLE_TASK:         return "TASK";
    default:                  return "UNKNOWN";
    }
}

static const char *ipc_state_str(ipc_state_t s)
{
    switch (s) {
    case IPC_NONE:     return "NONE";
    case IPC_SENDER:   return "SENDER";
    case IPC_RECEIVER: return "RECEIVER";
    case IPC_WAITING:  return "WAITING";
    default:           return "UNKNOWN";
    }
}

/* ------------------------------------------------------------------ */
/* Backtrace                                                           */
/* ------------------------------------------------------------------ */

typedef struct {
    uint32_t addresses[BACKTRACE_MAX_DEPTH];
    int      depth;
} backtrace_t;

static void backtrace_walk(backtrace_t *bt)
{
    bt->depth = 0;

    uint32_t fp;
    if (panic_fault_ctx.frame)
        fp = (*arch_reg(panic_fault_ctx.frame, 11));  /* fp captured at fault time */
    else
        __asm__ volatile("mov %0, r11" : "=r"(fp));

    while (bt->depth < BACKTRACE_MAX_DEPTH) {
        /* Any aligned kernel VA is potentially a valid frame */
        if (fp == 0 || (fp & 0x3u) || fp < KERNEL_VA_BASE)
            break;
        /* GCC ARM AAPCS prologue: [fp] = saved lr, [fp-4] = saved fp */
        uint32_t lr = *(uint32_t *)(uintptr_t)fp;
        bt->addresses[bt->depth++] = lr;
        uint32_t prev_fp = *(uint32_t *)(uintptr_t)(fp - 4);
        if (prev_fp <= fp)
            break;
        fp = prev_fp;
    }
}

/* ------------------------------------------------------------------ */
/* Heap snapshot                                                       */
/* ------------------------------------------------------------------ */

typedef struct {
    size_t used_bytes;
    size_t free_bytes;
    size_t total_bytes;
    size_t block_count;
} panic_heap_stats_t;

static void panic_heap_snapshot(panic_heap_stats_t *st)
{
    memset(st, 0, sizeof(*st));
    kmem_block_t *block = heap_head;
    size_t seen = 0;
    while (block && seen < 8192) {
        st->block_count++;
        st->total_bytes += block->size;
        if (block->free)
            st->free_bytes += block->size;
        else
            st->used_bytes += block->size;
        block = block->next;
        seen++;
    }
}

/* ------------------------------------------------------------------ */
/* Symbol helpers                                                      */
/* ------------------------------------------------------------------ */

static void sym_format(char *buf, int bufsz, uint32_t addr)
{
    const char *name = ksym_lookup(addr);
    uint32_t   base = ksym_lookup_base(addr);
    if (name && base && addr != base)
        snprintf(buf, (size_t)bufsz, "%s+0x%X", name, addr - base);
    else if (name)
        snprintf(buf, (size_t)bufsz, "%s", name);
    else
        snprintf(buf, (size_t)bufsz, "<?>");
}

/* ------------------------------------------------------------------ */
/* Logo                                                                */
/* ------------------------------------------------------------------ */

static const char *panic_logo[] = {
    "      " C_GRAY "@@@@@@" C_RESET,
    "        " C_GRAY "%@@" C_RESET "         " C_GRAY "%@@@  @" C_RESET,
    "       " C_GRAY "@@" C_RESET "           " C_GRAY "@@" C_RESET "   " C_GRAY "@@" C_RESET,
    "     " C_GRAY "@@" C_RESET "             " C_GRAY "@" C_RESET "    " C_GRAY "@=" C_RESET,
    "    " C_GRAY "@@@@@@@@@@@@@@" C_RESET " " C_GRAY "@@" C_RESET "   " C_GRAY "@@" C_RESET,
    "        " C_GRAY "@@" C_RESET "          " C_GRAY "@@@@" C_RESET,
    "        " C_GRAY "@@" C_RESET "             " C_GRAY "@" C_RESET,
    "        " C_GRAY "@@  %@@@@@@@  *@" C_RESET,
    "        " C_GRAY "@@  @" C_RESET "      " C_GRAY "@@ @@" C_RESET,
    "        " C_GRAY "@@  @" C_RESET "      " C_GRAY "@@ @@" C_RESET,
    "         " C_GRAY "@@@" C_RESET "        " C_GRAY "@@@" C_RESET,
};
#define PANIC_LOGO_LINES (sizeof(panic_logo) / sizeof(panic_logo[0]))

/* ================================================================== */
/* Sections                                                            */
/* ================================================================== */

/* ------------------------------------------------------------------ */
/* HEADER                                                              */
/* ------------------------------------------------------------------ */

static void panic_print_header(const char *reason, void *caller_ra)
{
    char line[LINE_BUF];
    char sym[80];

    for (size_t i = 0; i < PANIC_LOGO_LINES; i++) {
        panic_puts("  ");
        panic_puts(panic_logo[i]);
        panic_nl();
    }
    panic_nl();

    /* Top-level banner — red, same dash-rule format as section headers */
    panic_puts(C_RED "KERNEL PANIC ");
    for (int i = 13; i < RULE_COL; i++)
        uart_putc('-');
    panic_puts(C_RESET "\n");
    panic_nl();

    snprintf(line, sizeof(line), "reason:  %s", reason ? reason : "unknown");
    panic_line(line);

    if (!current_thread) {
        panic_line("context: BOOT");
    } else {
        process_t *p = current_thread->owner_process;
        if (p)
            snprintf(line, sizeof(line),
                     "context: PROCESS  [pid=%u  %s  tid=%u]",
                     p->pid, p->name, current_thread->tid);
        else
            snprintf(line, sizeof(line),
                     "context: PROCESS  [tid=%u  no owner]",
                     current_thread->tid);
        panic_line(line);
    }

    sym_format(sym, sizeof(sym), (uint32_t)(uintptr_t)caller_ra);
    snprintf(line, sizeof(line), "caller:  0x%08X  %s",
             (uint32_t)(uintptr_t)caller_ra, sym);
    panic_line(line);
}

/* ------------------------------------------------------------------ */
/* FAULT                                                               */
/* ------------------------------------------------------------------ */

static void panic_print_fault(void)
{
    if (!panic_fault_ctx.valid)
        return;

    char line[LINE_BUF];
    char dec[64];

    panic_section("FAULT");

    if (panic_fault_ctx.fault_type) {
        snprintf(line, sizeof(line), "type:    %s", panic_fault_ctx.fault_type);
        panic_line(line);
    }
    if (panic_fault_ctx.fault_decoded) {
        snprintf(line, sizeof(line), "decoded: %s", panic_fault_ctx.fault_decoded);
        panic_line(line);
    }
    if (panic_fault_ctx.access_type) {
        snprintf(line, sizeof(line), "access:  %s", panic_fault_ctx.access_type);
        panic_line(line);
    }

    if (panic_fault_ctx.far || panic_fault_ctx.fsr) {
        panic_nl();
        const char *region = far_region(panic_fault_ctx.far);
        if (region)
            snprintf(line, sizeof(line), "FAR:   0x%08X  [%s]",
                     panic_fault_ctx.far, region);
        else
            snprintf(line, sizeof(line), "FAR:   0x%08X", panic_fault_ctx.far);
        panic_line(line);

        snprintf(line, sizeof(line), "FSR:   0x%08X", panic_fault_ctx.fsr);
        panic_line(line);
    }

    /* SPSR tells us what mode was running when the fault occurred */
    if (panic_fault_ctx.frame) {
        cpsr_decode(dec, sizeof(dec), arch_regs_flags(panic_fault_ctx.frame));
        panic_nl();
        snprintf(line, sizeof(line), "SPSR:  0x%08X  %s  (interrupted context)",
                 arch_regs_flags(panic_fault_ctx.frame), dec);
        panic_line(line);
    }
}

/* ------------------------------------------------------------------ */
/* CPU STATE                                                           */
/* ------------------------------------------------------------------ */

static void panic_print_cpu(void)
{
    if (!panic_fault_ctx.frame)
        return;

    char line[LINE_BUF];
    char sym[80];
    arch_regs_t *f = panic_fault_ctx.frame;

    panic_section("CPU STATE");

    sym_format(sym, sizeof(sym), arch_regs_pc(f));
    snprintf(line, sizeof(line), "pc:     0x%08X  %s", arch_regs_pc(f), sym);
    panic_line(line);

    sym_format(sym, sizeof(sym), arch_regs_lr(f));
    snprintf(line, sizeof(line), "lr_usr: 0x%08X  %s", arch_regs_lr(f), sym);
    panic_line(line);

    {
        char dec[64];
        cpsr_decode(dec, sizeof(dec), arch_regs_flags(f));
        snprintf(line, sizeof(line), "cpsr:   0x%08X  %s", arch_regs_flags(f), dec);
        panic_line(line);
    }

    panic_nl();
    snprintf(line, sizeof(line),
             "r0  = %08X   r1  = %08X   r2  = %08X   r3  = %08X",
             (*arch_reg(f, 0)), (*arch_reg(f, 1)), (*arch_reg(f, 2)), (*arch_reg(f, 3)));
    panic_line(line);
    snprintf(line, sizeof(line),
             "r4  = %08X   r5  = %08X   r6  = %08X   r7  = %08X",
             (*arch_reg(f, 4)), (*arch_reg(f, 5)), (*arch_reg(f, 6)), (*arch_reg(f, 7)));
    panic_line(line);
    snprintf(line, sizeof(line),
             "r8  = %08X   r9  = %08X   r10 = %08X   r11 = %08X",
             (*arch_reg(f, 8)), (*arch_reg(f, 9)), (*arch_reg(f, 10)), (*arch_reg(f, 11)));
    panic_line(line);
    snprintf(line, sizeof(line),
             "r12 = %08X   sp_usr = %08X   lr_usr = %08X",
             (*arch_reg(f, 12)), arch_regs_sp(f), arch_regs_lr(f));
    panic_line(line);
}

/* ------------------------------------------------------------------ */
/* BACKTRACE                                                           */
/* ------------------------------------------------------------------ */

static void panic_print_backtrace(backtrace_t *bt)
{
    char line[LINE_BUF];
    char sym[80];

    panic_section("BACKTRACE");

    if (bt->depth == 0) {
        panic_line("(no frames)");
        return;
    }

    for (int i = 0; i < bt->depth; i++) {
        uint32_t addr = bt->addresses[i];
        sym_format(sym, sizeof(sym), addr);
        snprintf(line, sizeof(line), "#%-2d  0x%08X  %s", i, addr, sym);
        panic_line(line);
    }

    /* addr2line hint — print piece-by-piece to avoid buffer constraints */
    panic_nl();
    panic_puts("  " C_DIM "addr2line -e build/zuzu.elf");
    for (int i = 0; i < bt->depth; i++) {
        char tmp[12];
        snprintf(tmp, sizeof(tmp), " 0x%08X", bt->addresses[i]);
        panic_puts(tmp);
    }
    panic_puts(C_RESET "\n");
}

/* ------------------------------------------------------------------ */
/* CURRENT PROCESS  (PANIC_SECTION_PROCESS)                           */
/* ------------------------------------------------------------------ */

#ifdef PANIC_SECTION_PROCESS
static void panic_print_process(void)
{
    char line[LINE_BUF];

    panic_section("CURRENT PROCESS");

    if (!current_thread) {
        panic_line("(no current thread - BOOT context)");
        return;
    }

    process_t *p = current_thread->owner_process;

    snprintf(line, sizeof(line),
             "tid=%-4u  state=%-7s  prio=%u  slice=%u  left=%u",
             current_thread->tid,
             thread_state_str(current_thread->state),
             current_thread->priority,
             current_thread->time_slice,
             current_thread->ticks_remaining);
    panic_line(line);

    if (p) {
        snprintf(line, sizeof(line),
                 "pid=%-4u  ppid=%-4u  name=%s",
                 p->pid, p->parent_pid, p->name);
        panic_line(line);

        if (p->as) {
            snprintf(line, sizeof(line),
                     "asid=%u  ttbr0=0x%08X",
                     p->as->asid_token.asid, (uint32_t)p->as->ttbr0_pa);
            panic_line(line);
        }

        snprintf(line, sizeof(line),
                 "user stack: 0x%08X..0x%08X",
                 (uint32_t)USER_STACK_BASE, (uint32_t)USER_STACK_TOP);
        panic_line(line);

        /* Handle table */
        handle_vec_t *ht = &p->handle_table;
        if (ht->data && ht->cap > 0) {
            int shown = 0;
            panic_nl();
            panic_line("handles:");
            for (uint32_t idx = 1; idx < ht->cap && shown < PANIC_HANDLE_MAX; idx++) {
                handle_entry_t *e = &ht->data[idx];
                if (e->type == HANDLE_FREE)
                    continue;
                void *ptr = NULL;
                switch (e->type) {
                case HANDLE_ENDPOINT:     ptr = e->ep;    break;
                case HANDLE_DEVICE:       ptr = e->dev;   break;
                case HANDLE_SHMEM:        ptr = e->shm;   break;
                case HANDLE_REPLY:        ptr = e->reply; break;
                case HANDLE_NOTIFICATION: ptr = e->ntfn;  break;
                case HANDLE_TASK:         ptr = e->task;  break;
                default:                                   break;
                }
                snprintf(line, sizeof(line), "  [%2u]  %-14s  0x%08X",
                         idx, handle_type_str(e->type),
                         (uint32_t)(uintptr_t)ptr);
                panic_line(line);
                shown++;
            }
            if (shown == 0)
                panic_line("  (empty)");
        }
    }

    /* User trapframe (saved at syscall/exception entry) */
    if (current_thread->trap_frame) {
        arch_regs_t *tf = current_thread->trap_frame;
        panic_nl();
        panic_line("user trapframe:");
        snprintf(line, sizeof(line),
                 "  r0  = %08X   r1  = %08X   r2  = %08X   r3  = %08X",
                 (*arch_reg(tf, 0)), (*arch_reg(tf, 1)), (*arch_reg(tf, 2)), (*arch_reg(tf, 3)));
        panic_line(line);
        snprintf(line, sizeof(line),
                 "  r4  = %08X   r5  = %08X   r6  = %08X   r7  = %08X",
                 (*arch_reg(tf, 4)), (*arch_reg(tf, 5)), (*arch_reg(tf, 6)), (*arch_reg(tf, 7)));
        panic_line(line);
        snprintf(line, sizeof(line),
                 "  r8  = %08X   r9  = %08X   r10 = %08X   r11 = %08X",
                 (*arch_reg(tf, 8)), (*arch_reg(tf, 9)), (*arch_reg(tf, 10)), (*arch_reg(tf, 11)));
        panic_line(line);
        snprintf(line, sizeof(line),
                 "  r12 = %08X   sp_usr = %08X   lr_usr = %08X   pc = %08X",
                 (*arch_reg(tf, 12)), arch_regs_sp(tf), arch_regs_lr(tf), arch_regs_pc(tf));
        panic_line(line);
        {
            char dec[64];
            cpsr_decode(dec, sizeof(dec), arch_regs_flags(tf));
            snprintf(line, sizeof(line), "  cpsr = %08X  %s", arch_regs_flags(tf), dec);
            panic_line(line);
        }
    }

    /* IPC state */
    if (current_thread->ipc_state != IPC_NONE) {
        panic_nl();
        if (current_thread->blocked_endpoint)
            snprintf(line, sizeof(line), "IPC: %s  ep=0x%08X",
                     ipc_state_str(current_thread->ipc_state),
                     (uint32_t)(uintptr_t)current_thread->blocked_endpoint);
        else
            snprintf(line, sizeof(line), "IPC: %s",
                     ipc_state_str(current_thread->ipc_state));
        panic_line(line);
    }
}
#endif /* PANIC_SECTION_PROCESS */

/* ------------------------------------------------------------------ */
/* SCHEDULER  (PANIC_SECTION_SCHEDULER)                               */
/* ------------------------------------------------------------------ */

#ifdef PANIC_SECTION_SCHEDULER
static void panic_print_sched(void)
{
    char line[LINE_BUF];

    panic_section("SCHEDULER");

    if (current_thread) {
        process_t *p = current_thread->owner_process;
        snprintf(line, sizeof(line),
                 "current: tid=%-4u  pid=%-4u  %-16s  %s  prio=%u",
                 current_thread->tid,
                 p ? p->pid : 0u,
                 p ? p->name : "(none)",
                 thread_state_str(current_thread->state),
                 current_thread->priority);
    } else {
        snprintf(line, sizeof(line), "current: (idle)");
    }
    panic_line(line);

    /* Ready queue */
    thread_t *ready[PANIC_READY_MAX];
    size_t ready_total = sched_ready_queue_snapshot(ready, PANIC_READY_MAX);
    panic_nl();
    snprintf(line, sizeof(line), "ready (%lu):", (unsigned long)ready_total);
    panic_line(line);
    if (ready_total == 0) {
        panic_line("  (empty)");
    } else {
        size_t show = ready_total < PANIC_READY_MAX ? ready_total : PANIC_READY_MAX;
        for (size_t i = 0; i < show; i++) {
            thread_t *t = ready[i];
            process_t *p = t->owner_process;
            snprintf(line, sizeof(line),
                     "  tid=%-4u  pid=%-4u  %-16s  prio=%u",
                     t->tid, p ? p->pid : 0u,
                     p ? p->name : "(none)",
                     t->priority);
            panic_line(line);
        }
        if (ready_total > PANIC_READY_MAX) {
            snprintf(line, sizeof(line), "  ... +%lu more",
                     (unsigned long)(ready_total - PANIC_READY_MAX));
            panic_line(line);
        }
    }

    /* Sleep queue */
    panic_nl();
    {
        int sleep_count = 0;
        list_node_t *node;
        list_for_each(node, &sleep_queue.node)
            sleep_count++;

        snprintf(line, sizeof(line), "sleeping (%d):", sleep_count);
        panic_line(line);

        if (sleep_count == 0) {
            panic_line("  (empty)");
        } else {
            int shown = 0;
            list_for_each(node, &sleep_queue.node) {
                if (shown >= PANIC_SLEEP_MAX) {
                    snprintf(line, sizeof(line), "  ... +%d more",
                             sleep_count - shown);
                    panic_line(line);
                    break;
                }
                thread_t *t = container_of(node, thread_t, timeout_node);
                process_t *p = t->owner_process;
                snprintf(line, sizeof(line),
                         "  tid=%-4u  pid=%-4u  %-16s  wake_tick=%llu",
                         t->tid, p ? p->pid : 0u,
                         p ? p->name : "(none)",
                         (unsigned long long)t->wake_tick);
                panic_line(line);
                shown++;
            }
        }
    }
}
#endif /* PANIC_SECTION_SCHEDULER */

/* ------------------------------------------------------------------ */
/* IRQ / GIC  (PANIC_SECTION_IRQ)                                     */
/* ------------------------------------------------------------------ */

#ifdef PANIC_SECTION_IRQ
static void panic_print_irq(void)
{
    char line[LINE_BUF];

    panic_section("IRQ / GIC");

    if (!arch_irq_ready()) {
        panic_line("interrupt controller not yet initialized");
        return;
    }

#define IRQ_WORDS (MAX_IRQS / 32u)

    uint32_t pmr = arch_irq_priority_mask();
    snprintf(line, sizeof(line), "priority mask: 0x%02X  (%s)",
             pmr, pmr == 0xFFu ? "all priorities pass" : "filtered");
    panic_line(line);

    const irq_owner_t *owners = irq_panic_owners();

    /*
     * Snapshot enabled bitmap; used both for the enabled section and
     * later to identify the likely triggering IRQ in the pending section.
     * Skip SGIs (0-15) — always enabled on the controller.
     */
    uint32_t enabled_words[IRQ_WORDS];
    for (uint32_t w = 0; w < IRQ_WORDS; w++)
        enabled_words[w] = arch_irq_enabled_word(w);

    panic_nl();
    panic_line("enabled IRQs:");
    int any_enabled = 0;
    for (uint32_t word = 0; word < IRQ_WORDS; word++) {
        uint32_t ena = enabled_words[word];
        if (!ena) continue;
        for (uint32_t bit = 0; bit < 32u; bit++) {
            if (!(ena & (1u << bit))) continue;
            uint32_t irq = word * 32u + bit;
            if (irq < 16u) continue;  /* SGI — always on, skip */

            if (owners && irq < MAX_IRQS && owners[irq].owner) {
                snprintf(line, sizeof(line), "  IRQ %-3u  [%s]",
                         irq, owners[irq].owner->name);
            } else if (irq < (uint32_t)MAX_IRQS && handler_table[irq]) {
                char sym[64];
                sym_format(sym, sizeof(sym),
                            (uint32_t)(uintptr_t)handler_table[irq]);
                snprintf(line, sizeof(line), "  IRQ %-3u  [kernel: %s]", irq, sym);
            } else {
                snprintf(line, sizeof(line), "  IRQ %-3u  [no handler]", irq);
            }
            panic_line(line);
            any_enabled = 1;
        }
    }
    if (!any_enabled)
        panic_line("  (none)");

    /* Pending IRQs: skip SGIs; cross-reference enabled bitmap */
    uint32_t cpsr;
    __asm__ volatile("mrs %0, cpsr" : "=r"(cpsr));
    int in_irq_mode = ((cpsr & 0x1Fu) == 0x12u);

    panic_nl();
    panic_line("pending IRQs:");
    int any_pending = 0;
    for (uint32_t word = 0; word < IRQ_WORDS; word++) {
        uint32_t pend = arch_irq_pending_word(word);
        if (!pend) continue;
        for (uint32_t bit = 0; bit < 32u; bit++) {
            if (!(pend & (1u << bit))) continue;
            uint32_t irq = word * 32u + bit;
            if (irq < 16u) continue;  /* SGI — skip */

            int has_handler = (irq < (uint32_t)MAX_IRQS &&
                               handler_table[irq] != NULL);
            int also_enabled = (enabled_words[word] & (1u << bit)) != 0;

            if (!has_handler) {
                snprintf(line, sizeof(line),
                         "  IRQ %-3u" C_RED "  *** NO HANDLER ***" C_RESET, irq);
            } else if (also_enabled && in_irq_mode) {
                snprintf(line, sizeof(line),
                         "  IRQ %-3u" C_RED "  *** triggered this panic ***" C_RESET, irq);
            } else {
                snprintf(line, sizeof(line), "  IRQ %-3u  (pending, unserviced)", irq);
            }
            panic_line(line);
            any_pending = 1;
        }
    }
    if (!any_pending)
        panic_line("  (none)");
}
#endif /* PANIC_SECTION_IRQ */

/* ------------------------------------------------------------------ */
/* MEMORY  (PANIC_SECTION_MEMORY)                                     */
/* ------------------------------------------------------------------ */

#ifdef PANIC_SECTION_MEMORY
static void panic_print_memory(void)
{
    char line[LINE_BUF];

    panic_section("MEMORY");

    snprintf(line, sizeof(line),
             "PMM:   %lu / %lu pages free  (%lu KB free)",
             (unsigned long)pmm_state.free_pages,
             (unsigned long)pmm_state.total_pages,
             (unsigned long)(pmm_state.free_pages * 4u));
    panic_line(line);

    if (heap_head && kernel_layout.heap_start_va && kernel_layout.heap_end_va) {
        panic_heap_stats_t hs;
        panic_heap_snapshot(&hs);
        snprintf(line, sizeof(line),
                 "heap:  used=%lu  free=%lu  total=%lu  blocks=%lu",
                 (unsigned long)hs.used_bytes,
                 (unsigned long)hs.free_bytes,
                 (unsigned long)hs.total_bytes,
                 (unsigned long)hs.block_count);
        panic_line(line);
    } else {
        panic_line("heap:  unavailable");
    }

    if (kernel_layout.stack_base_va && kernel_layout.stack_top_va) {
        if (!current_thread) {
            panic_line("kstack: N/A (BOOT context)");
        } else {
            snprintf(line, sizeof(line),
                     "kstack: 0x%08X..0x%08X  (%lu KB)",
                     (uint32_t)kernel_layout.stack_base_va,
                     (uint32_t)kernel_layout.stack_top_va,
                     (unsigned long)((kernel_layout.stack_top_va -
                                      kernel_layout.stack_base_va) / 1024u));
            panic_line(line);
        }
    }
}
#endif /* PANIC_SECTION_MEMORY */

/* ================================================================== */
/* Entry point                                                         */
/* ================================================================== */

static void panic_screen(const char *reason, void *caller_ra)
{
    uart_putc('\a');
    panic_nl();

    panic_print_header(reason, caller_ra);
    panic_print_fault();
    panic_print_cpu();

    backtrace_t bt;
    backtrace_walk(&bt);
    panic_print_backtrace(&bt);

#ifdef PANIC_SECTION_PROCESS
    panic_print_process();
#endif
#ifdef PANIC_SECTION_SCHEDULER
    panic_print_sched();
#endif
#ifdef PANIC_SECTION_IRQ
    panic_print_irq();
#endif
#ifdef PANIC_SECTION_MEMORY
    panic_print_memory();
#endif

    panic_nl();
}

_Noreturn void __attribute__((cold)) panic(const char *reason)
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

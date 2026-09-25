// panic.c - Kernel panic screen

#include "core/panic.h"
#include "kernel/task/task.h"
#include <arch/backtrace.h>
#include <arch/cpu.h>
#include <arch/irq.h>
#include <arch/regs.h>
#include <arch/symbols.h>
#include BOARD_LAYOUT_H /* KERNEL_VA_BASE, USER_VA_TOP, IOREMAP_BASE */
#include "drivers/uart/uart.h"
#include "kernel/layout.h"
#include "kernel/mm/alloc.h"
#include "kernel/mm/pmm/pmm.h"
#include "kernel/sched/sched.h"
#include "kernel/space/handle.h"
#include "kernel/space/space.h"
#include "ksym.h"
#include <list.h>
#include <snprintf.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>

#ifdef CONFIG_PANIC_SECTION_IRQ
#include "kernel/irq/irq_relay.h"
#endif

extern kernel_layout_t kernel_layout;

panic_fault_context_t panic_fault_ctx;

/* ------------------------------------------------------------------ */
/* Constants                                                           */
/* ------------------------------------------------------------------ */

#define BACKTRACE_MAX_DEPTH 16
#define PANIC_READY_MAX 8
#define PANIC_SLEEP_MAX 8
#define PANIC_HANDLE_MAX 16
#define RULE_COL 72
#define LINE_BUF 160

/*
 * Virtual-layout bounds for pointer sanity checks. KERNEL_VA_BASE / USER_VA_TOP
 * come from the board layout; the MMIO window base maps to IOREMAP_BASE.
 */
#ifndef MMIO_VA_BASE
#define MMIO_VA_BASE IOREMAP_BASE
#endif

/* ------------------------------------------------------------------ */
/* Colors                                                              */
/* ------------------------------------------------------------------ */

#define C_YELLOW "\033[33m" /* section headers */
#define C_GRAY "\033[37m"   /* normal content  */
#define C_DIM "\033[90m"    /* low-emphasis    */
#define C_RED "\033[91m"    /* errors only     */
#define C_RESET "\033[0m"

/* Legacy aliases used in the logo */
#define C_AMBER C_YELLOW
#define C_WHITE C_GRAY

/* ------------------------------------------------------------------ */
/* Low-level output                                                    */
/* ------------------------------------------------------------------ */

static void PanicPuts(const char *s)
{
    while (*s)
        uart_putc(*s++);
}

static void PanicNewline(void) { uart_putc('\n'); }

/* Section header: yellow name + dash rule to column RULE_COL */
static void PanicHeader(const char *name)
{
    PanicPuts("\n" C_YELLOW);
    PanicPuts(name);
    int n = (int)strlen(name);
    uart_putc(' ');
    for (int i = n + 1; i < RULE_COL; i++)
        uart_putc('-');
    PanicPuts(C_RESET "\n");
}

/* 2-space indent, gray content */
static void PanicPutLine(const char *s)
{
    PanicPuts("  " C_GRAY);
    PanicPuts(s);
    PanicPuts(C_RESET "\n");
}

/* ------------------------------------------------------------------ */
/* FAR range annotation                                                */
/* ------------------------------------------------------------------ */

static const char *FarRegion(uint32_t addr)
{

    if (addr >= (uint32_t)(uintptr_t)_kernel_start && addr < (uint32_t)(uintptr_t)_kernel_end)
        return "kernel text/data";

    if (kernel_layout.stack_base_va && addr >= (uint32_t)kernel_layout.stack_base_va &&
        addr < (uint32_t)kernel_layout.stack_top_va)
        return "kernel stack";

    if (kernel_layout.heap_start_va && addr >= (uint32_t)(uintptr_t)kernel_layout.heap_start_va &&
        addr < (uint32_t)(uintptr_t)kernel_layout.heap_end_va)
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

static const char *TaskStateToString(TaskState state)
{
    switch (state)
    {
    case READY:
        return "READY";
    case RUNNING:
        return "RUNNING";
    case BLOCKED:
        return "BLOCKED";
    case ZOMBIE:
        return "ZOMBIE";
    case FROZEN:
        return "FROZEN";
    default:
        return "UNKNOWN";
    }
}

static const char *HandleTypeStr(HandleType t)
{
    switch (t)
    {
    case HANDLE_FREE:
        return "FREE";
    case HANDLE_PORT:
        return "PORT";
    case HANDLE_MEM:
        return "MEMORY";
    case HANDLE_REPLY:
        return "REPLY";
    case HANDLE_EVENT:
        return "EVENT";
    case HANDLE_TASK:
        return "TASK";
    case HANDLE_SPACE:
        return "SPACE";
    default:
        return "UNKNOWN";
    }
}

static const char *IpcStateStr(MsgState s)
{
    switch (s)
    {
    case IPC_NONE:
        return "NONE";
    case IPC_SENDER:
        return "SENDER";
    case IPC_RECEIVER:
        return "RECEIVER";
    case IPC_WAITING:
        return "WAITING";
    default:
        return "UNKNOWN";
    }
}

/* ------------------------------------------------------------------ */
/* Backtrace                                                           */
/* ------------------------------------------------------------------ */

typedef struct
{
    uint32_t addresses[BACKTRACE_MAX_DEPTH];
    int depth;
} FpBacktrace;

static void BtraceWalk(FpBacktrace *bt)
{
    Register fp = panic_fault_ctx.frame ? arch_regs_fp(panic_fault_ctx.frame) /* fp at fault time */
                                        : arch_current_fp();

    bt->depth =
        (int)arch_backtrace_walk(fp, (Register)KERNEL_VA_BASE, bt->addresses, BACKTRACE_MAX_DEPTH);
}

/* ------------------------------------------------------------------ */
/* Heap snapshot                                                       */
/* ------------------------------------------------------------------ */

typedef struct
{
    size_t used_bytes;
    size_t free_bytes;
    size_t total_bytes;
    size_t block_count;
} PanicHeapStats;

static void GetPanicHeapSnapshot(PanicHeapStats *st)
{
    memset(st, 0, sizeof(*st));
    KMemBlock *block = heap_head;
    size_t seen = 0;
    while (block && seen < 8192)
    {
        st->block_count++;
        st->total_bytes += block->size;
        if (block->state == KBLOCK_FREE)
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

static void FormatKSym(char *buf, int bufsz, uint32_t addr)
{
    const char *name = ksym_lookup(addr);
    uint32_t base = ksym_lookup_base(addr);
    if (name && base && addr != base)
        (void)snprintf(buf, (size_t)bufsz, "%s+0x%X", name, addr - base);
    else if (name)
        (void)snprintf(buf, (size_t)bufsz, "%s", name);
    else
        (void)snprintf(buf, (size_t)bufsz, "<?>");
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

static void PanicPrintHeader(const char *reason, void *caller_ra)
{
    char line[LINE_BUF];
    char sym[80];

    for (size_t i = 0; i < PANIC_LOGO_LINES; i++)
    {
        PanicPuts("  ");
        PanicPuts(panic_logo[i]);
        PanicNewline();
    }
    PanicNewline();

    /* Top-level banner — red, same dash-rule format as section headers */
    PanicPuts(C_RED "KERNEL PANIC ");
    for (int i = 13; i < RULE_COL; i++)
        uart_putc('-');
    PanicPuts(C_RESET "\n");
    PanicNewline();

    (void)snprintf(line, sizeof(line), "reason:  %s", reason ? reason : "unknown");
    PanicPutLine(line);

    if (!current_task)
    {
        PanicPutLine("context: BOOT");
    }
    else
    {
        SpaceObject *space = current_task->owner;
        if (space)
            (void)snprintf(line, sizeof(line), "context: SPACE  [pid=%d  %s  tid=%u]", space->spid,
                           space->name, current_task->tid);
        else
            (void)snprintf(line, sizeof(line), "context: TASK  [tid=%u  no owner]",
                           current_task->tid);
        PanicPutLine(line);
    }

    FormatKSym(sym, sizeof(sym), (uint32_t)(uintptr_t)caller_ra);
    (void)snprintf(line, sizeof(line), "caller:  0x%08X  %s", (uint32_t)(uintptr_t)caller_ra, sym);
    PanicPutLine(line);
}

/* ------------------------------------------------------------------ */
/* FAULT                                                               */
/* ------------------------------------------------------------------ */

static void PanicPrintFault(void)
{
    if (!panic_fault_ctx.valid)
        return;

    char line[LINE_BUF];
    char dec[64];

    PanicHeader("FAULT");

    if (panic_fault_ctx.fault_type)
    {
        (void)snprintf(line, sizeof(line), "type:    %s", panic_fault_ctx.fault_type);
        PanicPutLine(line);
    }
    if (panic_fault_ctx.fault_decoded)
    {
        (void)snprintf(line, sizeof(line), "decoded: %s", panic_fault_ctx.fault_decoded);
        PanicPutLine(line);
    }
    if (panic_fault_ctx.access_type)
    {
        (void)snprintf(line, sizeof(line), "access:  %s", panic_fault_ctx.access_type);
        PanicPutLine(line);
    }

    if (panic_fault_ctx.far || panic_fault_ctx.fsr)
    {
        PanicNewline();
        const char *region = FarRegion(panic_fault_ctx.far);
        if (region)
            (void)snprintf(line, sizeof(line), "FAR:   0x%08X  [%s]", panic_fault_ctx.far, region);
        else
            (void)snprintf(line, sizeof(line), "FAR:   0x%08X", panic_fault_ctx.far);
        PanicPutLine(line);

        (void)snprintf(line, sizeof(line), "FSR:   0x%08X", panic_fault_ctx.fsr);
        PanicPutLine(line);
    }

    /* SPSR tells us what mode was running when the fault occurred */
    if (panic_fault_ctx.frame)
    {
        arch_flags_decode(dec, sizeof(dec), arch_regs_flags(panic_fault_ctx.frame));
        PanicNewline();
        (void)snprintf(line, sizeof(line), "SPSR:  0x%08X  %s  (interrupted context)",
                       arch_regs_flags(panic_fault_ctx.frame), dec);
        PanicPutLine(line);
    }
}

/* ------------------------------------------------------------------ */
/* CPU STATE                                                           */
/* ------------------------------------------------------------------ */

static void PanicDumpCpuState(void)
{
    if (!panic_fault_ctx.frame)
        return;

    char line[LINE_BUF];
    char sym[80];
    CpuState *f = panic_fault_ctx.frame;

    PanicHeader("CPU STATE");

    FormatKSym(sym, sizeof(sym), (VirtAddr)arch_regs_pc(f));
    (void)snprintf(line, sizeof(line), "pc:     0x%08X  %s", arch_regs_pc(f), sym);
    PanicPutLine(line);

    FormatKSym(sym, sizeof(sym), (VirtAddr)arch_regs_lr(f));
    (void)snprintf(line, sizeof(line), "lr:     0x%08X  %s", arch_regs_lr(f), sym);
    PanicPutLine(line);

    (void)snprintf(line, sizeof(line), "sp:     0x%08X", arch_regs_sp(f));
    PanicPutLine(line);

    {
        char dec[64];
        arch_flags_decode(dec, sizeof(dec), arch_regs_flags(f));
        (void)snprintf(line, sizeof(line), "flags:  0x%08X  %s", arch_regs_flags(f), dec);
        PanicPutLine(line);
    }

    PanicNewline();
    for (unsigned i = 0; i < ARCH_NUM_GP_REGS; i += 4)
    {
        int off = snprintf(line, sizeof(line), "reg[%2u] = %08X", i, (*ArchGetFromFrame(f, i)));
        for (unsigned j = i + 1; j < i + 4 && j < ARCH_NUM_GP_REGS; j++)
            off += snprintf(line + off, sizeof(line) - (size_t)off, "   reg[%2u] = %08X", j,
                            (*ArchGetFromFrame(f, j)));
        PanicPutLine(line);
    }
}

/* ------------------------------------------------------------------ */
/* BACKTRACE                                                           */
/* ------------------------------------------------------------------ */

static void PanicPrintBt(FpBacktrace *bt)
{
    char line[LINE_BUF];
    char sym[80];

    PanicHeader("BACKTRACE");

    if (bt->depth == 0)
    {
        PanicPutLine("(no frames)");
        return;
    }

    for (int i = 0; i < bt->depth; i++)
    {
        uint32_t addr = bt->addresses[i];
        FormatKSym(sym, sizeof(sym), addr);
        (void)snprintf(line, sizeof(line), "#%-2d  0x%08X  %s", i, addr, sym);
        PanicPutLine(line);
    }

    /* addr2line hint — print piece-by-piece to avoid buffer constraints */
    PanicNewline();
    PanicPuts("  " C_DIM "addr2line -e " ZUZU_ELF_PATH);
    for (int i = 0; i < bt->depth; i++)
    {
        char tmp[12];
        (void)snprintf(tmp, sizeof(tmp), " 0x%08X", bt->addresses[i]);
        PanicPuts(tmp);
    }
    PanicPuts(C_RESET "\n");
}

/* ------------------------------------------------------------------ */
/* CURRENT PROCESS  (CONFIG_PANIC_SECTION_PROCESS)                           */
/* ------------------------------------------------------------------ */

#ifdef CONFIG_PANIC_SECTION_PROCESS
static void PanicPrintSpace(void)
{
    char line[LINE_BUF];

    PanicHeader("CURRENT PROCESS");

    if (!current_task)
    {
        PanicPutLine("(no current thread - BOOT context)");
        return;
    }

    SpaceObject *space = current_task->owner;

    (void)snprintf(line, sizeof(line), "tid=%-4u  state=%-7s  prio=%u  slice=%u  left=%u",
                   current_task->tid, TaskStateToString(current_task->state),
                   current_task->priority, current_task->time_slice, current_task->ticks_remaining);
    PanicPutLine(line);

    if (space)
    {
        (void)snprintf(line, sizeof(line), "pid=%-4d  ppid=%-4d  name=%s", space->spid,
                       space->parent_spid, space->name);
        PanicPutLine(line);

        if (space->as)
        {
            (void)snprintf(line, sizeof(line), "asid=%u  ttbr0=0x%08X", space->as->asid_token.asid,
                           (uint32_t)space->as->pt_root_physaddr);
            PanicPutLine(line);
        }

        (void)snprintf(line, sizeof(line), "user stack: 0x%08X..0x%08X", (uint32_t)USER_STACK_BASE,
                       (uint32_t)USER_STACK_TOP);
        PanicPutLine(line);

        /* Handle table */
        HandleTable *ht = &space->handle_table;
        {
            int shown = 0;
            PanicNewline();
            PanicPutLine("handles:");
            for (Handle idx = 1; idx < (Handle)HANDLE_MAX_SLOTS && shown < PANIC_HANDLE_MAX; idx++)
            {
                HandleTableEntry *e = HandleTableGet(ht, idx);
                if (!e || e->type == HANDLE_FREE)
                    continue;
                void *ptr = NULL;
                switch (e->type)
                {
                case HANDLE_PORT:
                    ptr = e->port;
                    break;
                case HANDLE_MEM:
                    ptr = e->mem;
                    break;
                case HANDLE_REPLY:
                    ptr = e->reply;
                    break;
                case HANDLE_EVENT:
                    ptr = e->event;
                    break;
                case HANDLE_TASK:
                    ptr = e->task;
                    break;
                case HANDLE_SPACE:
                    ptr = e->space;
                    break;
                default:
                    break;
                }
                (void)snprintf(line, sizeof(line), "  [%2u]  %-14s  0x%08X", idx,
                               HandleTypeStr(e->type), (uint32_t)(uintptr_t)ptr);
                PanicPutLine(line);
                shown++;
            }
            if (shown == 0)
                PanicPutLine("  (empty)");
        }
    }

    /* User trapframe (saved at syscall/exception entry) */
    if (current_task->trap_frame)
    {
        CpuState *tf = current_task->trap_frame;
        PanicNewline();
        PanicPutLine("user trapframe:");
        for (unsigned i = 0; i < ARCH_NUM_GP_REGS; i += 4)
        {
            int off =
                snprintf(line, sizeof(line), "  reg[%2u] = %08X", i, (*ArchGetFromFrame(tf, i)));
            for (unsigned j = i + 1; j < i + 4 && j < ARCH_NUM_GP_REGS; j++)
                off += snprintf(line + off, sizeof(line) - (size_t)off, "   reg[%2u] = %08X", j,
                                (*ArchGetFromFrame(tf, j)));
            PanicPutLine(line);
        }
        (void)snprintf(line, sizeof(line), "  sp = %08X   lr = %08X   pc = %08X", arch_regs_sp(tf),
                       arch_regs_lr(tf), arch_regs_pc(tf));
        PanicPutLine(line);
        {
            char dec[64];
            arch_flags_decode(dec, sizeof(dec), arch_regs_flags(tf));
            (void)snprintf(line, sizeof(line), "  cpsr = %08X  %s", arch_regs_flags(tf), dec);
            PanicPutLine(line);
        }
    }

    /* IPC state */
    if (current_task->ipc_state != IPC_NONE)
    {
        PanicNewline();
        if (current_task->blocked_port)
            (void)snprintf(line, sizeof(line), "IPC: %s  port=0x%08X",
                     IpcStateStr(current_task->ipc_state),
                     (uint32_t)(uintptr_t)current_task->blocked_port);
        else
            (void)snprintf(line, sizeof(line), "IPC: %s", IpcStateStr(current_task->ipc_state));
        PanicPutLine(line);
    }
}
#endif /* CONFIG_PANIC_SECTION_PROCESS */

/* ------------------------------------------------------------------ */
/* SCHEDULER  (CONFIG_PANIC_SECTION_SCHEDULER)                               */
/* ------------------------------------------------------------------ */

#ifdef CONFIG_PANIC_SECTION_SCHEDULER
static void PanicDumpSched(void)
{
    char line[LINE_BUF];

    PanicHeader("SCHEDULER");

    if (current_task)
    {
        SpaceObject *space = current_task->owner;
        (void)snprintf(line, sizeof(line), "current: tid=%-4u  spid=%-4d  %-16s  %s  prio=%u",
                       current_task->tid, space ? space->spid : 0, space ? space->name : "(none)",
                       TaskStateToString(current_task->state), current_task->priority);
    }
    else
    {
        (void)snprintf(line, sizeof(line), "current: (idle)");
    }
    PanicPutLine(line);

    /* Ready queue */
    TaskObject *ready[PANIC_READY_MAX];
    size_t ready_total = SchedGetReadyQueue(ready, PANIC_READY_MAX);
    PanicNewline();
    (void)snprintf(line, sizeof(line), "ready (%lu):", (unsigned long)ready_total);
    PanicPutLine(line);
    if (ready_total == 0)
    {
        PanicPutLine("  (empty)");
    }
    else
    {
        size_t show = ready_total < PANIC_READY_MAX ? ready_total : PANIC_READY_MAX;
        for (size_t i = 0; i < show; i++)
        {
            TaskObject *t = ready[i];
            SpaceObject *p = t->owner;
            (void)snprintf(line, sizeof(line), "  tid=%-4u  spid=%-4d  %-16s  prio=%u", t->tid,
                     p ? p->spid : 0, p ? p->name : "(none)", t->priority);
            PanicPutLine(line);
        }
        if (ready_total > PANIC_READY_MAX)
        {
            (void)snprintf(line, sizeof(line), "  ... +%lu more",
                     (unsigned long)(ready_total - PANIC_READY_MAX));
            PanicPutLine(line);
        }
    }

    /* Sleep wheel */
    TaskObject *sleepers[PANIC_SLEEP_MAX];
    size_t sleep_total = SchedGetSleepers(sleepers, PANIC_SLEEP_MAX);
    PanicNewline();
    (void)snprintf(line, sizeof(line), "sleeping (%lu):", (unsigned long)sleep_total);
    PanicPutLine(line);

    if (sleep_total == 0)
    {
        PanicPutLine("  (empty)");
    }
    else
    {
        size_t show = sleep_total < PANIC_SLEEP_MAX ? sleep_total : PANIC_SLEEP_MAX;
        for (size_t i = 0; i < show; i++)
        {
            TaskObject *t = sleepers[i];
            SpaceObject *p = t->owner;
            (void)snprintf(line, sizeof(line), "  tid=%-4u  spid=%-4d  %-16s  wake_deadline=%llu",
                           t->tid, p ? p->spid : 0, p ? p->name : "(none)",
                           (unsigned long long)t->wake_deadline);
            PanicPutLine(line);
        }
        if (sleep_total > PANIC_SLEEP_MAX)
        {
            (void)snprintf(line, sizeof(line), "  ... +%lu more",
                           (unsigned long)(sleep_total - PANIC_SLEEP_MAX));
            PanicPutLine(line);
        }
    }
}
#endif /* CONFIG_PANIC_SECTION_SCHEDULER */

/* ------------------------------------------------------------------ */
/* IRQ / GIC  (CONFIG_PANIC_SECTION_IRQ)                                     */
/* ------------------------------------------------------------------ */

#ifdef CONFIG_PANIC_SECTION_IRQ
static void PanicPrintIrq(void)
{
    char line[LINE_BUF];

    PanicHeader("IRQ / GIC");

    if (!arch_irq_ready())
    {
        PanicPutLine("interrupt controller not yet initialized");
        return;
    }

#define IRQ_WORDS (MAX_IRQS / 32u)

    uint32_t pmr = arch_irq_priority_mask();
    (void)snprintf(line, sizeof(line), "priority mask: 0x%02X  (%s)", pmr,
                   pmr == 0xFFU ? "all priorities pass" : "filtered");
    PanicPutLine(line);

    const IrqOwner *owners = GetIrqOwnersList();

    /*
     * Snapshot enabled bitmap; used both for the enabled section and
     * later to identify the likely triggering IRQ in the pending section.
     * Skip SGIs (0-15) — always enabled on the controller.
     */
    uint32_t enabled_words[IRQ_WORDS];
    for (uint32_t w = 0; w < IRQ_WORDS; w++)
        enabled_words[w] = arch_irq_enabled_word(w);

    PanicNewline();
    PanicPutLine("enabled IRQs:");
    int any_enabled = 0;
    for (uint32_t word = 0; word < IRQ_WORDS; word++)
    {
        uint32_t ena = enabled_words[word];
        if (!ena)
            continue;
        for (uint32_t bit = 0; bit < 32U; bit++)
        {
            if (!(ena & (1U << bit)))
                continue;
            uint32_t irq = (word * 32U) + bit;
            if (irq < 16U)
                continue; /* SGI - always on, skip */

            if (owners && irq < MAX_IRQS && owners[irq].owner)
            {
                (void)snprintf(line, sizeof(line), "  IRQ %-3u  [%s]", irq,
                               owners[irq].owner->name);
            }
            else if (ArchIrqHasHandler(irq))
            {
                char sym[64];
                FormatKSym(sym, sizeof(sym), (uint32_t)(uintptr_t)arch_irq_handler_addr(irq));
                (void)snprintf(line, sizeof(line), "  IRQ %-3u  [kernel: %s]", irq, sym);
            }
            else
            {
                (void)snprintf(line, sizeof(line), "  IRQ %-3u  [no handler]", irq);
            }
            PanicPutLine(line);
            any_enabled = 1;
        }
    }
    if (!any_enabled)
        PanicPutLine("  (none)");

    /* Pending IRQs: skip SGIs; cross-reference enabled bitmap */
    Register cpsr = arch_current_flags();
    bool in_irq_mode = arch_flags_in_irq_context(cpsr);

    PanicNewline();
    PanicPutLine("pending IRQs:");
    int any_pending = 0;
    for (uint32_t word = 0; word < IRQ_WORDS; word++)
    {
        uint32_t pend = arch_irq_pending_word(word);
        if (!pend)
            continue;
        for (uint32_t bit = 0; bit < 32U; bit++)
        {
            if (!(pend & (1U << bit)))
                continue;
            uint32_t irq = (word * 32U) + bit;
            if (irq < 16U)
                continue;

            bool has_handler = ArchIrqHasHandler(irq);
            bool also_enabled = (enabled_words[word] & (1U << bit)) != 0;

            if (!has_handler)
            {
                (void)snprintf(line, sizeof(line),
                               "  IRQ %-3u" C_RED "  *** NO HANDLER ***" C_RESET, irq);
            }
            else if (also_enabled && in_irq_mode)
            {
                (void)snprintf(line, sizeof(line),
                               "  IRQ %-3u" C_RED "  *** triggered this panic ***" C_RESET, irq);
            }
            else
            {
                (void)snprintf(line, sizeof(line), "  IRQ %-3u  (pending, unserviced)", irq);
            }
            PanicPutLine(line);
            any_pending = 1;
        }
    }
    if (!any_pending)
        PanicPutLine("  (none)");
}
#endif /* CONFIG_PANIC_SECTION_IRQ */

/* ------------------------------------------------------------------ */
/* MEMORY  (CONFIG_PANIC_SECTION_MEMORY)                                     */
/* ------------------------------------------------------------------ */

#ifdef CONFIG_PANIC_SECTION_MEMORY
static void PanicPrintMemoryStats(void)
{
    char line[LINE_BUF];

    PanicHeader("MEMORY");

    PmmStats pmm_stats = PmmGetStats();
    (void)snprintf(line, sizeof(line), "PMM:   %lu / %lu pages free  (%lu KB free)",
                   (unsigned long)pmm_stats.free_frames, (unsigned long)pmm_stats.total_frames,
                   (unsigned long)(pmm_stats.free_frames * 4U));
    PanicPutLine(line);

    if (heap_head && kernel_layout.heap_start_va && kernel_layout.heap_end_va)
    {
        PanicHeapStats hs;
        GetPanicHeapSnapshot(&hs);
        (void)snprintf(line, sizeof(line), "heap:  used=%lu  free=%lu  total=%lu  blocks=%lu",
                       (unsigned long)hs.used_bytes, (unsigned long)hs.free_bytes,
                       (unsigned long)hs.total_bytes, (unsigned long)hs.block_count);
        PanicPutLine(line);
    }
    else
    {
        PanicPutLine("heap:  unavailable");
    }

    if (kernel_layout.stack_base_va && kernel_layout.stack_top_va)
    {
        if (!current_task)
        {
            PanicPutLine("kstack: N/A (BOOT context)");
        }
        else
        {
            (void)snprintf(
                line, sizeof(line), "kstack: 0x%08X..0x%08X  (%lu KB)",
                (uint32_t)kernel_layout.stack_base_va, (uint32_t)kernel_layout.stack_top_va,
                (unsigned long)((kernel_layout.stack_top_va - kernel_layout.stack_base_va) /
                                1024U));
            PanicPutLine(line);
        }
    }
}
#endif /* CONFIG_PANIC_SECTION_MEMORY */

/* ================================================================== */
/* Entry point                                                         */
/* ================================================================== */

static void ConstructPanicScreen(const char *reason, void *caller_ra)
{
    uart_putc('\a');
    PanicNewline();

    PanicPrintHeader(reason, caller_ra);
    PanicPrintFault();
    PanicDumpCpuState();

    FpBacktrace bt;
    BtraceWalk(&bt);
    PanicPrintBt(&bt);

#ifdef CONFIG_PANIC_SECTION_PROCESS
    PanicPrintSpace();
#endif
#ifdef CONFIG_PANIC_SECTION_SCHEDULER
    PanicDumpSched();
#endif
#ifdef CONFIG_PANIC_SECTION_IRQ
    PanicPrintIrq();
#endif
#ifdef CONFIG_PANIC_SECTION_MEMORY
    PanicPrintMemoryStats();
#endif

    PanicNewline();
}

bool entered_panic = false;

_Noreturn void __attribute__((cold)) panic(const char *fmt, ...)
{

    void *caller_ra;

    arch_global_irq_disable();

    if (entered_panic)
        goto panic_loop;

    entered_panic = true;

    /* Static: panic is terminal and runs with IRQs off, so no reentrancy */
    static char reason[LINE_BUF];

    caller_ra = __builtin_return_address(0);

    if (fmt)
    {
        va_list ap;
        va_start(ap, fmt);
        (void)vsnprintf(reason, sizeof(reason), fmt, ap);
        va_end(ap);
    }

    ConstructPanicScreen(fmt ? reason : NULL, caller_ra);

panic_loop:
    __asm__ volatile("1:\n"
                     "    wfi\n"
                     "    b 1b\n");
    __builtin_unreachable();
}

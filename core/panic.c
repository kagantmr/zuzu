/**
 * @file panic.c
 * @brief Kernel panic: renders a diagnostic screen on the UART and halts the CPU.
 */
#include "core/panic.h"
#include "kernel/task/task.h"
#include <arch/backtrace.h>
#include <arch/cpu.h>
#include <arch/irq.h>
#include <arch/regs.h>
#include <arch/symbols.h>
#include BOARD_LAYOUT_H
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

/**
 * @brief Kernel memory layout (stack and heap bounds) used to classify and report addresses.
 */
extern RamLayout kernel_layout;

PanicFaultContext panic_fault_ctx;

/** @brief Maximum number of backtrace frames printed. */
#define BACKTRACE_MAX_DEPTH 16
/** @brief Maximum number of ready tasks listed on the panic screen. */
#define PANIC_READY_MAX 8
/** @brief Maximum number of sleeping tasks listed on the panic screen. */
#define PANIC_SLEEP_MAX 8
/** @brief Maximum number of handle table entries listed on the panic screen. */
#define PANIC_HANDLE_MAX 16
/** @brief Column at which section header rules end. */
#define RULE_COL 72
/** @brief Size of the line formatting buffers, in bytes. */
#define LINE_BUF 160

#ifndef MMIO_VA_BASE
/** @brief Lowest virtual address treated as MMIO/high kernel space when classifying FAR. */
#define MMIO_VA_BASE IOREMAP_BASE
#endif

/** @brief ANSI escape: yellow foreground. */
#define C_YELLOW "\033[33m"
/** @brief ANSI escape: light gray foreground. */
#define C_GRAY "\033[37m"
/** @brief ANSI escape: dim gray foreground. */
#define C_DIM "\033[90m"
/** @brief ANSI escape: bright red foreground. */
#define C_RED "\033[91m"
/** @brief ANSI escape: reset attributes. */
#define C_RESET "\033[0m"

/**
 * @brief Write a NUL-terminated string to the UART, bypassing the console layer.
 *
 * @param[in] s String to write.
 */
static void PanicPuts(const char *s)
{
    while (*s)
        uart_putc(*s++);
}

/**
 * @brief Write a newline to the UART.
 */
static void PanicNewline(void) { uart_putc('\n'); }

/**
 * @brief Print a yellow section title followed by a dash rule up to ::RULE_COL.
 *
 * @param[in] name Section title.
 */
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

/**
 * @brief Print one indented gray line to the UART.
 *
 * @param[in] s Line text, without trailing newline.
 */
static void PanicPutLine(const char *s)
{
    PanicPuts("  " C_GRAY);
    PanicPuts(s);
    PanicPuts(C_RESET "\n");
}

/**
 * @brief Classify a fault address into a coarse memory region.
 *
 * @param[in] addr Faulting virtual address.
 * @return Region description, or NULL if the address matches no known region.
 */
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

/**
 * @brief Convert a task state to its display name.
 *
 * @param[in] state Task state.
 * @return Upper-case state name, or "UNKNOWN".
 */
static const char *TaskStateToString(TaskState state)
{
    switch (state) {
    case TASK_STATE_READY:
        return "READY";
    case TASK_STATE_RUNNING:
        return "RUNNING";
    case TASK_STATE_BLOCKED:
        return "BLOCKED";
    case TASK_STATE_ZOMBIE:
        return "ZOMBIE";
    case TASK_STATE_FROZEN:
        return "FROZEN";
    default:
        return "UNKNOWN";
    }
}

/**
 * @brief Convert a handle type to its display name.
 *
 * @param[in] t Handle type.
 * @return Upper-case type name, or "UNKNOWN".
 */
static const char *HandleTypeStr(HandleType t)
{
    switch (t) {
    case HANDLE_FREE:
        return "FREE";
    case HANDLE_PORT:
        return "PORT";
    case HANDLE_MEM:
        return "MEMORY";
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

/**
 * @brief Convert an IPC state to its display name.
 *
 * @param[in] s IPC state.
 * @return Upper-case state name, or "UNKNOWN".
 */
static const char *IpcStateStr(IpcState s)
{
    switch (s) {
    case IPC_NONE:
        return "NONE";
    case IPC_WAITING:
        return "WAITING";
    default:
        return "UNKNOWN";
    }
}

/** @brief Captured frame-pointer backtrace. */
typedef struct {
    uint32_t addresses[BACKTRACE_MAX_DEPTH];  /**< Return addresses, innermost first. */
    int depth;                                /**< Number of valid entries in addresses. */
} FpBacktrace;

/**
 * @brief Walk the frame-pointer chain and record return addresses.
 *  *
 *  * Starts from the fault frame pointer when a fault context frame exists, otherwise from the current frame pointer.
 *
 * @param[in] bt Backtrace to fill.
 */
static void BtraceWalk(FpBacktrace *bt)
{
    Register fp = panic_fault_ctx.frame ? ArchFrameFp(panic_fault_ctx.frame)
                                        : ArchCurrentFramePointer();

    bt->depth =
        (int)ArchBacktraceWalk(fp, (Register)KERNEL_VA_BASE, bt->addresses, BACKTRACE_MAX_DEPTH);
}

/** @brief Heap usage totals gathered for the panic screen. */
typedef struct {
    size_t used_bytes;   /**< Bytes in allocated blocks. */
    size_t free_bytes;   /**< Bytes in free blocks. */
    size_t total_bytes;  /**< Sum of all block sizes. */
    size_t block_count;  /**< Number of blocks walked. */
} PanicHeapStats;

/**
 * @brief Compute heap usage by walking the heap block list.
 * 
 * Walks at most 8192 blocks to stay safe on a corrupted list.
 *
 * @param[in] st Statistics to fill; zeroed first.
 */
static void GetPanicHeapSnapshot(PanicHeapStats *st)
{
    memset(st, 0, sizeof(*st));
    KHeapBlock *block = heap_head;
    size_t seen = 0;
    while (block && seen < 8192) {
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

/**
 * @brief Format an address as "name+0xOFFSET", "name", or "<?>" when unresolved.
 *
 * @param[in] buf Output buffer.
 * @param[in] bufsz Size of @p buf in bytes.
 * @param[in] addr Address to symbolize.
 */
static void FormatKSym(char *buf, int bufsz, uint32_t addr)
{
    const char *name = KSymLookup(addr);
    uint32_t base = KSymLookupBaseAddr(addr);
    if (name && base && addr != base)
        (void)snprintf(buf, (size_t)bufsz, "%s+0x%X", name, addr - base);
    else if (name)
        (void)snprintf(buf, (size_t)bufsz, "%s", name);
    else
        (void)snprintf(buf, (size_t)bufsz, "<?>");
}

/** @brief ASCII-art logo lines printed at the top of the panic screen. */
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
/** @brief Number of lines in ::panic_logo. */
#define PANIC_LOGO_LINES (sizeof(panic_logo) / sizeof(panic_logo[0]))

/**
 * @brief Print the logo, banner, reason, execution context and caller address.
 *
 * @param[in] reason Panic reason text, or NULL for "unknown".
 * @param[in] caller_ra Return address of the panic() caller.
 */
static void PanicPrintHeader(const char *reason, void *caller_ra)
{
    char line[LINE_BUF];
    char sym[80];

    for (size_t i = 0; i < PANIC_LOGO_LINES; i++) {
        PanicPuts("  ");
        PanicPuts(panic_logo[i]);
        PanicNewline();
    }
    PanicNewline();

    PanicPuts(C_RED "Oops! zuzu has panicked. ");
    for (int i = 13; i < RULE_COL; i++)
        uart_putc('-');
    PanicPuts(C_RESET "\n");
    PanicNewline();

    (void)snprintf(line, sizeof(line), "reason:  %s", reason ? reason : "unknown");
    PanicPutLine(line);

    if (!current_task) {
        PanicPutLine("context: BOOT");
    } else {
        SpaceObject *space = current_task->owner;
        if (space)
            (void)snprintf(line, sizeof(line), "context: SPACE  [spid=%d  %s  tid=%u]", space->spid,
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

/**
 * @brief Print the FAULT section (type, decoding, access, FAR, FSR, SPSR) when ::panic_fault_ctx is valid; otherwise print nothing.
 */
static void PanicPrintFault(void)
{
    if (!panic_fault_ctx.valid)
        return;

    char line[LINE_BUF];
    char dec[64];

    PanicHeader("FAULT");

    if (panic_fault_ctx.fault_type) {
        (void)snprintf(line, sizeof(line), "type:    %s", panic_fault_ctx.fault_type);
        PanicPutLine(line);
    }
    if (panic_fault_ctx.fault_decoded) {
        (void)snprintf(line, sizeof(line), "decoded: %s", panic_fault_ctx.fault_decoded);
        PanicPutLine(line);
    }
    if (panic_fault_ctx.access_type) {
        (void)snprintf(line, sizeof(line), "access:  %s", panic_fault_ctx.access_type);
        PanicPutLine(line);
    }

    if (panic_fault_ctx.far || panic_fault_ctx.fsr) {
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

    if (panic_fault_ctx.frame) {
        ArchFlagsDecode(dec, sizeof(dec), ArchFrameFlags(panic_fault_ctx.frame));
        PanicNewline();
        (void)snprintf(line, sizeof(line), "SPSR:  0x%08X  %s  (interrupted context)",
                       ArchFrameFlags(panic_fault_ctx.frame), dec);
        PanicPutLine(line);
    }
}

/**
 * @brief Print the CPU STATE section (pc, lr, sp, flags and general registers) of the fault frame; prints nothing when there is no frame.
 */
static void PanicDumpCpuState(void)
{
    if (!panic_fault_ctx.frame)
        return;

    char line[LINE_BUF];
    char sym[80];
    CpuState *f = panic_fault_ctx.frame;

    PanicHeader("CPU STATE");

    FormatKSym(sym, sizeof(sym), (VirtAddr)ArchFramePc(f));
    (void)snprintf(line, sizeof(line), "pc:     0x%08X  %s", ArchFramePc(f), sym);
    PanicPutLine(line);

    FormatKSym(sym, sizeof(sym), (VirtAddr)ArchFrameLr(f));
    (void)snprintf(line, sizeof(line), "lr:     0x%08X  %s", ArchFrameLr(f), sym);
    PanicPutLine(line);

    (void)snprintf(line, sizeof(line), "sp:     0x%08X", ArchFrameSp(f));
    PanicPutLine(line);

    {
        char dec[64];
        ArchFlagsDecode(dec, sizeof(dec), ArchFrameFlags(f));
        (void)snprintf(line, sizeof(line), "flags:  0x%08X  %s", ArchFrameFlags(f), dec);
        PanicPutLine(line);
    }

    PanicNewline();
    for (unsigned i = 0; i < ARCH_NUM_GP_REGS; i += 4) {
        int off = snprintf(line, sizeof(line), "reg[%2u] = %08X", i, (*ArchGetFromFrame(f, i)));
        for (unsigned j = i + 1; j < i + 4 && j < ARCH_NUM_GP_REGS; j++)
            off += snprintf(line + off, sizeof(line) - (size_t)off, "   reg[%2u] = %08X", j,
                            (*ArchGetFromFrame(f, j)));
        PanicPutLine(line);
    }
}

/**
 * @brief Print the BACKTRACE section and an addr2line command line for the recorded frames.
 *
 * @param[in] bt Backtrace to print.
 */
static void PanicPrintBt(FpBacktrace *bt)
{
    char line[LINE_BUF];
    char sym[80];

    PanicHeader("BACKTRACE");

    if (bt->depth == 0) {
        PanicPutLine("(no frames)");
        return;
    }

    for (int i = 0; i < bt->depth; i++) {
        uint32_t addr = bt->addresses[i];
        FormatKSym(sym, sizeof(sym), addr);
        (void)snprintf(line, sizeof(line), "#%-2d  0x%08X  %s", i, addr, sym);
        PanicPutLine(line);
    }

    PanicNewline();
    PanicPuts("  " C_DIM "addr2line -e " ZUZU_ELF_PATH);
    for (int i = 0; i < bt->depth; i++) {
        char tmp[12];
        (void)snprintf(tmp, sizeof(tmp), " 0x%08X", bt->addresses[i]);
        PanicPuts(tmp);
    }
    PanicPuts(C_RESET "\n");
}

#ifdef CONFIG_PANIC_SECTION_PROCESS
/**
 * @brief Print the CURRENT SPACE section: task and space identity, address space, user stack, handle table, user trap frame and IPC state.
 */
static void PanicPrintSpace(void)
{
    char line[LINE_BUF];

    PanicHeader("CURRENT SPACE");

    if (!current_task) {
        PanicPutLine("(no current task - BOOT context)");
        return;
    }

    SpaceObject *space = current_task->owner;

    (void)snprintf(line, sizeof(line), "tid=%-4u  state=%-7s  prio=%u  slice=%u", current_task->tid,
                   TaskStateToString(current_task->state), current_task->priority,
                   current_task->time_slice_us);
    PanicPutLine(line);

    if (space) {
        (void)snprintf(line, sizeof(line), "spid=%-4d  parent=%-4d  name=%s", space->spid,
                       space->parent_spid, space->name);
        PanicPutLine(line);

        if (space->as) {
            (void)snprintf(line, sizeof(line), "asid=%u  ttbr0=0x%08X", space->as->asid_token.asid,
                           (uint32_t)space->as->pt_root_physaddr);
            PanicPutLine(line);
        }

        (void)snprintf(line, sizeof(line), "user stack: 0x%08X..0x%08X", (uint32_t)USER_STACK_BASE,
                       (uint32_t)USER_STACK_TOP);
        PanicPutLine(line);

        HandleTable *ht = &space->handle_table;
        {
            int shown = 0;
            PanicNewline();
            PanicPutLine("handles:");
            for (Handle idx = 1; idx < (Handle)HANDLE_MAX_SLOTS && shown < PANIC_HANDLE_MAX;
                 idx++) {
                HandleTableEntry *e = HandleTableGet(ht, idx);
                if (!e || e->type == HANDLE_FREE)
                    continue;
                void *ptr = NULL;
                switch (e->type) {
                case HANDLE_PORT:
                    ptr = e->port;
                    break;
                case HANDLE_MEM:
                    ptr = e->mem;
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

    if (current_task->trap_frame) {
        CpuState *tf = current_task->trap_frame;
        PanicNewline();
        PanicPutLine("user trapframe:");
        for (unsigned i = 0; i < ARCH_NUM_GP_REGS; i += 4) {
            int off =
                snprintf(line, sizeof(line), "  reg[%2u] = %08X", i, (*ArchGetFromFrame(tf, i)));
            for (unsigned j = i + 1; j < i + 4 && j < ARCH_NUM_GP_REGS; j++)
                off += snprintf(line + off, sizeof(line) - (size_t)off, "   reg[%2u] = %08X", j,
                                (*ArchGetFromFrame(tf, j)));
            PanicPutLine(line);
        }
        (void)snprintf(line, sizeof(line), "  sp = %08X   lr = %08X   pc = %08X", ArchFrameSp(tf),
                       ArchFrameLr(tf), ArchFramePc(tf));
        PanicPutLine(line);
        {
            char dec[64];
            ArchFlagsDecode(dec, sizeof(dec), ArchFrameFlags(tf));
            (void)snprintf(line, sizeof(line), "  cpsr = %08X  %s", ArchFrameFlags(tf), dec);
            PanicPutLine(line);
        }
    }

    if (current_task->ipc_state != IPC_NONE) {
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

#ifdef CONFIG_PANIC_SECTION_SCHEDULER
/**
 * @brief Print the SCHEDULER section: current task plus truncated ready and sleeping task lists.
 */
static void PanicDumpSched(void)
{
    char line[LINE_BUF];

    PanicHeader("SCHEDULER");

    if (current_task) {
        SpaceObject *space = current_task->owner;
        (void)snprintf(line, sizeof(line), "current: tid=%-4u  spid=%-4d  %-16s  %s  prio=%u",
                       current_task->tid, space ? space->spid : 0, space ? space->name : "(none)",
                       TaskStateToString(current_task->state), current_task->priority);
    } else {
        (void)snprintf(line, sizeof(line), "current: (idle)");
    }
    PanicPutLine(line);

    TaskObject *ready[PANIC_READY_MAX];
    size_t ready_total = SchedGetReadyQueue(ready, PANIC_READY_MAX);
    PanicNewline();
    (void)snprintf(line, sizeof(line), "ready (%lu):", (unsigned long)ready_total);
    PanicPutLine(line);
    if (ready_total == 0) {
        PanicPutLine("  (empty)");
    } else {
        size_t show = ready_total < PANIC_READY_MAX ? ready_total : PANIC_READY_MAX;
        for (size_t i = 0; i < show; i++) {
            TaskObject *t = ready[i];
            SpaceObject *p = t->owner;
            (void)snprintf(line, sizeof(line), "  tid=%-4u  spid=%-4d  %-16s  prio=%u", t->tid,
                           p ? p->spid : 0, p ? p->name : "(none)", t->priority);
            PanicPutLine(line);
        }
        if (ready_total > PANIC_READY_MAX) {
            (void)snprintf(line, sizeof(line), "  ... +%lu more",
                           (unsigned long)(ready_total - PANIC_READY_MAX));
            PanicPutLine(line);
        }
    }

    TaskObject *sleepers[PANIC_SLEEP_MAX];
    size_t sleep_total = SchedGetSleepers(sleepers, PANIC_SLEEP_MAX);
    PanicNewline();
    (void)snprintf(line, sizeof(line), "sleeping (%lu):", (unsigned long)sleep_total);
    PanicPutLine(line);

    if (sleep_total == 0) {
        PanicPutLine("  (empty)");
    } else {
        size_t show = sleep_total < PANIC_SLEEP_MAX ? sleep_total : PANIC_SLEEP_MAX;
        for (size_t i = 0; i < show; i++) {
            TaskObject *t = sleepers[i];
            SpaceObject *p = t->owner;
            (void)snprintf(line, sizeof(line), "  tid=%-4u  spid=%-4d  %-16s  wake_deadline=%llu",
                           t->tid, p ? p->spid : 0, p ? p->name : "(none)",
                           (unsigned long long)t->wake_deadline);
            PanicPutLine(line);
        }
        if (sleep_total > PANIC_SLEEP_MAX) {
            (void)snprintf(line, sizeof(line), "  ... +%lu more",
                           (unsigned long)(sleep_total - PANIC_SLEEP_MAX));
            PanicPutLine(line);
        }
    }
}
#endif /* CONFIG_PANIC_SECTION_SCHEDULER */

#ifdef CONFIG_PANIC_SECTION_IRQ
/**
 * @brief Print the IRQ / GIC section: priority mask, enabled IRQs with owners and pending IRQs.
 */
static void PanicPrintIrq(void)
{
    char line[LINE_BUF];

    PanicHeader("IRQ / GIC");

    if (!ArchIrqReady()) {
        PanicPutLine("interrupt controller not yet initialized");
        return;
    }

    uint32_t pmr = ArchIrqPriorityMask();
    (void)snprintf(line, sizeof(line), "priority mask: 0x%02X  (%s)", pmr,
                   pmr == 0xFFU ? "all priorities pass" : "filtered");
    PanicPutLine(line);

    const IrqOwner *owners = GetIrqOwnersList();
    uint32_t first = ArchIrqFirstDeviceLine();

    PanicNewline();
    PanicPutLine("enabled IRQs:");
    int any_enabled = 0;
    for (uint32_t irq = first; irq < MAX_IRQS; irq++) {
        if (!ArchIrqIsEnabled(irq))
            continue;

        if (owners && owners[irq].owner) {
            (void)snprintf(line, sizeof(line), "  IRQ %-3u  [%s]", irq, owners[irq].owner->name);
        } else if (ArchIrqHasHandler(irq)) {
            char sym[64];
            FormatKSym(sym, sizeof(sym), (uint32_t)(uintptr_t)ArchIrqHandlerAddr(irq));
            (void)snprintf(line, sizeof(line), "  IRQ %-3u  [kernel: %s]", irq, sym);
        } else {
            (void)snprintf(line, sizeof(line), "  IRQ %-3u  [no handler]", irq);
        }
        PanicPutLine(line);
        any_enabled = 1;
    }
    if (!any_enabled)
        PanicPutLine("  (none)");

    Register cpsr = ArchCurrentFlags();
    bool in_irq_mode = ArchFlagsInIrqContext(cpsr);

    PanicNewline();
    PanicPutLine("pending IRQs:");
    int any_pending = 0;
    for (uint32_t irq = first; irq < MAX_IRQS; irq++) {
        if (!ArchIrqIsPending(irq))
            continue;

        bool has_handler = ArchIrqHasHandler(irq);
        bool also_enabled = ArchIrqIsEnabled(irq);

        if (!has_handler) {
            (void)snprintf(line, sizeof(line),
                           "  IRQ %-3u" C_RED "  *** NO HANDLER ***" C_RESET, irq);
        } else if (also_enabled && in_irq_mode) {
            (void)snprintf(line, sizeof(line),
                           "  IRQ %-3u" C_RED "  *** triggered this panic ***" C_RESET, irq);
        } else {
            (void)snprintf(line, sizeof(line), "  IRQ %-3u  (pending, unserviced)", irq);
        }
        PanicPutLine(line);
        any_pending = 1;
    }
    if (!any_pending)
        PanicPutLine("  (none)");
}
#endif /* CONFIG_PANIC_SECTION_IRQ */

#ifdef CONFIG_PANIC_SECTION_MEMORY
/**
 * @brief Print the MEMORY section: PMM page counts, heap usage and kernel stack bounds.
 */
static void PanicPrintMemoryStats(void)
{
    char line[LINE_BUF];

    PanicHeader("MEMORY");

    PmmStats pmm_stats = PmmGetStats();
    (void)snprintf(line, sizeof(line), "PMM:   %lu / %lu pages free  (%lu KB free)",
                   (unsigned long)pmm_stats.free_frames, (unsigned long)pmm_stats.total_frames,
                   (unsigned long)(pmm_stats.free_frames * 4U));
    PanicPutLine(line);

    if (heap_head && kernel_layout.heap_start_va && kernel_layout.heap_end_va) {
        PanicHeapStats hs;
        GetPanicHeapSnapshot(&hs);
        (void)snprintf(line, sizeof(line), "heap:  used=%lu  free=%lu  total=%lu  blocks=%lu",
                       (unsigned long)hs.used_bytes, (unsigned long)hs.free_bytes,
                       (unsigned long)hs.total_bytes, (unsigned long)hs.block_count);
        PanicPutLine(line);
    } else {
        PanicPutLine("heap:  unavailable");
    }

    if (kernel_layout.stack_base_va && kernel_layout.stack_top_va) {
        if (!current_task) {
            PanicPutLine("kstack: N/A (BOOT context)");
        } else {
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

/**
 * @brief Print the full panic screen, including the optional sections enabled by configuration.
 *
 * @param[in] reason Panic reason text, or NULL.
 * @param[in] caller_ra Return address of the panic() caller.
 */
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

/** @brief Set on first entry to panic(); a nested panic halts without printing. */
static bool entered_panic = false;

_Noreturn void __attribute__((cold)) panic(const char *fmt, ...)
{

    void *caller_ra;

    ArchGlobalIrqDisable();

    if (entered_panic)
        goto panic_loop;

    entered_panic = true;

    static char reason[LINE_BUF];

    caller_ra = __builtin_return_address(0);

    if (fmt) {
        va_list ap;
        va_start(ap, fmt);
        (void)vsnprintf(reason, sizeof(reason), fmt, ap);
        va_end(ap);
    }

    ConstructPanicScreen(fmt ? reason : NULL, caller_ra);

panic_loop:
    ArchCpuHalt();
}

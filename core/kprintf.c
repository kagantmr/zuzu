#include "core/kprintf.h"
#include "core/log.h"
#include "arch/arm/include/irq.h"
#include "kernel/stats/stats.h"
#include "lib/string.h"
#include "lib/snprintf.h"

static void (*kernel_console_putc)(char);

void kprintf_init(void (*putc_func)(char)) {
    kernel_console_putc = putc_func;
}

#ifdef STATS_MODE

static char  klog_ring[KLOG_RING_LINES][KLOG_LINE_MAX];
static size_t klog_head  = 0;   /* next slot to write */
static size_t klog_count = 0;   /* total lines stored */

static void klog_ring_push(const char *msg)
{
    /* Strip trailing newline(s) so stats rendering is clean */
    size_t len = strnlen(msg, KLOG_LINE_MAX - 1);
    while (len > 0 && (msg[len - 1] == '\n' || msg[len - 1] == '\r'))
        len--;

    strncpy(klog_ring[klog_head], msg, len);
    klog_ring[klog_head][len] = '\0';

    klog_head = (klog_head + 1) % KLOG_RING_LINES;
    if (klog_count < KLOG_RING_LINES)
        klog_count++;
}

size_t klog_ring_count(void) { return klog_count; }

const char *klog_ring_line(size_t index)
{
    if (index >= klog_count) return "";
    size_t start = (klog_count < KLOG_RING_LINES)
                       ? 0
                       : klog_head;              /* oldest entry */
    return klog_ring[(start + index) % KLOG_RING_LINES];
}

#endif /* STATS_MODE */

void kprintf(const char* fmt, ...) {
#ifdef STATS_MODE
    if (stats_mode_active) {
        char buf[KLOG_LINE_MAX];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        klog_ring_push(buf);
        return;
    }
#endif
    uint32_t cpsr;
    __asm__ volatile("mrs %0, cpsr" : "=r"(cpsr));
    arch_global_irq_disable();
    va_list args;
    va_start(args, fmt);
    vstrfmt(kernel_console_putc, fmt, args);
    va_end(args);
    if (!(cpsr & (1 << 7))) {
        arch_global_irq_enable();  // only re-enable if they were enabled before
    }
}
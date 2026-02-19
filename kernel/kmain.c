#include "arch/arm/include/symbols.h"
#include "arch/arm/include/irq.h"
#include "arch/arm/include/gicv2.h"
#include "arch/arm/timer/generic_timer.h"

#include "arch/arm/mmu/mmu.h"
#include "arch/arm/vexpress-a15/board.h"

#include "drivers/uart/uart.h"
#include "drivers/uart/pl011.h"
#include "drivers/timer/sp804.h"

#include "core/version.h"

#include "core/kprintf.h"
#include "core/log.h"
#include "core/panic.h"
#include "core/assert.h"

#include "kernel/layout.h"
#include "kernel/mm/pmm.h"
#include "kernel/dtb/dtb.h"
#include "kernel/vmm/vmm.h"
#include "kernel/time/tick.h"
#include "kernel/sched/sched.h"
#include "kernel/proc/process.h"

#include "lib/mem.h"
#include "kernel/mm/alloc.h"

#include "fetch.h"

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

/* Helper: create a shared endpoint and assign to handle 0 of all given processes */
static endpoint_t *make_shared_endpoint(process_t *owner, process_t **procs, int count)
{
    endpoint_t *ep = kmalloc(sizeof(endpoint_t));
    memset(ep, 0, sizeof(endpoint_t));
    list_init(&ep->sender_queue);
    list_init(&ep->receiver_queue);
    ep->owner_pid = owner->pid;
    for (int i = 0; i < count; i++)
        procs[i]->handle_table[0] = ep;
    return ep;
}

static inline void perform_panic_tests(void) {
    /**
     * disclaimer: most of these tests will halt the kernel immediately, so don't uncomment ANY of these 
     * if you want your kernel to actually do something instead of saying "Oops! zuzu has halted.".
     */

    // test 1: undefined instruction panics the kernel with undef.
    //__asm__ volatile(".word 0xe7f000f0");

    // test 2: should give a prefetch abort, nothing is there to execute in 0.
    //__asm__ volatile("mov lr, #0x0\n" "bx lr\n");

    // test 3: simulate a reserved exception
    //__asm__ volatile("ldr r0, =0x14\n" "bx r0\n");

    // test 5: simulate a Fast Interrupt reQuest (FIQ).
    // test as of 19 Feb 2026: data aborted at FFFFFFFC, because I didn't map the FIQ stack... might as well remove it idk
    //__asm__ volatile("cps #0x11\n");

    // test 6: a bad access in kernel code should panic with a data abort.
    //__asm__ volatile("mov r0, #0x0\n" "str r0, [r0]\n");

    // test 7: a syscall should do absolutely nothing in SVC mode, used to crash, fixed by ignoring in SVC mode.
    //__asm__ volatile("svc #0");

    // test 8: just manually halt the system
    //panic("Test panic triggered (chewed wires)");
}

_Noreturn void kmain(void)
{
    __asm__ volatile("cpsid if");

    kernel_layout.dtb_start_va = (void *)PA_TO_VA(kernel_layout.dtb_start_pa);
    kernel_layout.stack_base_va = (uintptr_t)PA_TO_VA(kernel_layout.stack_base_pa);
    kernel_layout.stack_top_va = (uintptr_t)PA_TO_VA(kernel_layout.stack_top_pa);
    kernel_layout.kernel_start_va = (uintptr_t)_kernel_start;
    kernel_layout.kernel_end_va = (uintptr_t)_kernel_end;

    void *boot_dtb = kernel_layout.dtb_start_va;

    uint32_t magic = read_be32((uint8_t *)boot_dtb + 0x00);
    kassert(magic == 0xD00DFEED);
    (void)magic;

    uint32_t totalsize = read_be32((uint8_t *)boot_dtb + 0x04);
    kassert(totalsize >= 0x28 && totalsize < (1024u * 1024u));

    uint32_t dtb_pages = (totalsize + PAGE_SIZE - 1) / PAGE_SIZE;
    uintptr_t dtb_pa = pmm_alloc_pages(dtb_pages);
    if (!dtb_pa) {
        panic("Failed to allocate pages for DTB");
    }

    uint8_t *new_dtb = (uint8_t *)PA_TO_VA(dtb_pa);
    memcpy(new_dtb, boot_dtb, totalsize);

    kernel_layout.dtb_start_va = new_dtb;

    vmm_remove_identity_mapping();

    arch_mmu_init_ttbr1(vmm_get_kernel_as());
    vmm_lockdown_kernel_sections();

    if (kernel_layout.heap_start_va == NULL)
    {
        if (kernel_layout.heap_start_pa >= KERNEL_VA_BASE)
            kernel_layout.heap_start_va = (void *)kernel_layout.heap_start_pa;
        else
            kernel_layout.heap_start_va = (void *)PA_TO_VA(kernel_layout.heap_start_pa);
    }
    if (kernel_layout.heap_end_va == NULL)
    {
        if (kernel_layout.heap_end_pa >= KERNEL_VA_BASE)
            kernel_layout.heap_end_va = (void *)kernel_layout.heap_end_pa;
        else
            kernel_layout.heap_end_va = (void *)PA_TO_VA(kernel_layout.heap_end_pa);
    }

    dtb_init(kernel_layout.dtb_start_va);

    irq_init();
    board_init_devices();

    pl011_init_irq(uart_get_base());
    arch_global_irq_enable();

    print_boot_banner();

    perform_panic_tests();

    sched_init();

    /* ================================================================
     * IPC TEST SUITE
     * ================================================================ */

    KINFO("=== IPC Test Suite ===");

    /* --- Test A: Ping-Pong (bidirectional send/recv) --- */
    {
        process_t *pinger = process_create(NULL, 0xAAAA0001);
        process_t *ponger = process_create(NULL, 0xAAAA0002);
        process_t *group[] = { pinger, ponger };
        make_shared_endpoint(pinger, group, 2);
        sched_add(pinger);
        sched_add(ponger);
    }

    /* --- Test B: Sender blocks first, receiver delayed --- */
    {
        process_t *sender = process_create(NULL, 0xBBBB0001);
        process_t *receiver = process_create(NULL, 0xBBBB0002);
        process_t *group[] = { sender, receiver };
        make_shared_endpoint(sender, group, 2);
        sched_add(sender);
        sched_add(receiver);
    }

    /* --- Test C: Multiple senders, one receiver (FIFO) --- */
    {
        process_t *s1 = process_create(NULL, 0xCCCC0001);
        process_t *s2 = process_create(NULL, 0xCCCC0002);
        process_t *rcv = process_create(NULL, 0xCCCC0003);
        process_t *group[] = { s1, s2, rcv };
        make_shared_endpoint(rcv, group, 3);
        sched_add(s1);
        sched_add(s2);
        sched_add(rcv);
    }

    /* --- Test D: Invalid handle (error, no crash) --- */
    {
        process_t *bad = process_create(NULL, 0xDDDD0001);
        sched_add(bad);
    }

    /* Keep a spinner alive so scheduler always has something */
    {
        process_t *spinner = process_create(NULL, 0x12345678);
        sched_add(spinner);
    }

    register_tick_callback(schedule);
    KINFO("Entering idle");
    while (1)
    {
        sched_reap();
        __asm__("wfi");
    }
}
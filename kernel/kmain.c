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


_Noreturn void kmain(void)
{

    // This removes identity mapping and relocates ALL mode stacks to higher-half.
    // Must happen before ANY other code runs in kmain.
    //
    // Interrupts should already be disabled from early boot,
    // but disable them explicitly here for safety since we're
    // modifying IRQ/ABT/UND mode stack pointers.
    __asm__ volatile("cpsid if"); // Disable IRQ and FIQ

    // Establish VA companions for boot-time physical addresses while both mappings exist.
    // After identity removal, only *_va fields should be dereferenced.
    kernel_layout.dtb_start_va = (void *)PA_TO_VA(kernel_layout.dtb_start_pa);
    kernel_layout.stack_base_va = (uintptr_t)PA_TO_VA(kernel_layout.stack_base_pa);
    kernel_layout.stack_top_va = (uintptr_t)PA_TO_VA(kernel_layout.stack_top_pa);
    kernel_layout.kernel_start_va = (uintptr_t)_kernel_start;
    kernel_layout.kernel_end_va = (uintptr_t)_kernel_end;

    // Copy DTB into heap so it remains valid after identity mapping is removed.
    // DTB header fields are big-endian.
    void *boot_dtb = kernel_layout.dtb_start_va;

    uint32_t magic = read_be32((uint8_t *)boot_dtb + 0x00);
    kassert(magic == 0xD00DFEED);
    (void)magic;

    uint32_t totalsize = read_be32((uint8_t *)boot_dtb + 0x04);
    // Basic sanity: header is 0x28 bytes; DTBs for this platform should be well under 1 MiB.
    kassert(totalsize >= 0x28 && totalsize < (1024u * 1024u));

    uint32_t dtb_pages = (totalsize + PAGE_SIZE - 1) / PAGE_SIZE;
    uintptr_t dtb_pa = pmm_alloc_pages(dtb_pages);
    if (!dtb_pa) {
        panic("Failed to allocate pages for DTB");
    }

    

    // The pages are already mapped in kernel space via the higher-half mapping
    // (all of RAM is mapped at PA+0x40000000), so we can just use PA_TO_VA
    uint8_t *new_dtb = (uint8_t *)PA_TO_VA(dtb_pa);
    memcpy(new_dtb, boot_dtb, totalsize);

    kernel_layout.dtb_start_va = new_dtb;

    
    vmm_remove_identity_mapping();

    arch_mmu_init_ttbr1(vmm_get_kernel_as());
    vmm_lockdown_kernel_sections();

    // Ensure heap VA companions are populated for logging and dereferencing.
    // Some early bring-up paths may only have heap_*_pa set.
    if (kernel_layout.heap_start_va == NULL)
    {
        if (kernel_layout.heap_start_pa >= KERNEL_VA_BASE)
        {
            kernel_layout.heap_start_va = (void *)kernel_layout.heap_start_pa;
        }
        else
        {
            kernel_layout.heap_start_va = (void *)PA_TO_VA(kernel_layout.heap_start_pa);
        }
    }
    if (kernel_layout.heap_end_va == NULL)
    {
        if (kernel_layout.heap_end_pa >= KERNEL_VA_BASE)
        {
            kernel_layout.heap_end_va = (void *)kernel_layout.heap_end_pa;
        }
        else
        {
            kernel_layout.heap_end_va = (void *)PA_TO_VA(kernel_layout.heap_end_pa);
        }
    }

    // From this point onward, use dtb_start_va
    dtb_init(kernel_layout.dtb_start_va);

    irq_init();
    board_init_devices();


    pl011_init_irq(uart_get_base()); // Enable RX interrupts for UART
    arch_global_irq_enable(); // Only after GIC is initialized

    KINFO("Booting...");


    // data abort test (page fault)
    // *(volatile uint32_t *)0xDEADBEEF = 42;

    print_boot_banner();

    

    // trigger SVC to verify exception handling
    __asm__ volatile("svc #0");

    sched_init();

    // Prints hello, exits
    process_t *p_talk = process_create(NULL, 0xCAFEBABE);
    sched_add(p_talk);

    // Yields
    process_t *p_yield = process_create(NULL, 0xDEADBEEF);
    sched_add(p_yield);

    process_t *p_rx = process_create(NULL, 0x11111111);
    sched_add(p_rx);

    process_t *p_tx = process_create(NULL, 0x22222222);
    sched_add(p_tx);

    // Sleeps, wakes up, exits
    process_t *p_crash = process_create(NULL, 0xABABABAB);
    sched_add(p_crash);

    // infinite loop
    process_t *p_idle_work = process_create(NULL, 0x12345678); // Default spinner
    sched_add(p_idle_work);

    register_tick_callback(schedule);

    KINFO("Entering idle");
    while (1)
    {
        sched_reap();
        __asm__("wfi");
    }
}
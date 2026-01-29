#include "arch/arm/include/symbols.h"
#include "arch/arm/include/irq.h"
#include "arch/arm/include/gicv2.h"
#include "arch/arm/timer/generic_timer.h"

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

#include "lib/mem.h"
#include "kernel/mm/alloc.h"

#include "banner.h"

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

    // === CRITICAL: Complete VMM transition first ===
    // This removes identity mapping and relocates ALL mode stacks to higher-half.
    // Must happen before ANY other code runs in kmain.
    //
    // NOTE: Interrupts should already be disabled from early boot,
    // but we disable them explicitly here for safety since we're
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

    uint8_t *new_dtb = (uint8_t *)kmalloc(totalsize);
    kassert(new_dtb != NULL);
    memcpy(new_dtb, boot_dtb, totalsize);

    kernel_layout.dtb_start_va = new_dtb;

    vmm_remove_identity_mapping();

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

#ifdef EARLY_UART
    // UART already working from early boot - remap it to proper VA
    void *uart_va = ioremap(uart_get_base(), 0x1000);
    if (!uart_va)
    {
        KPANIC("Failed to ioremap UART");
    }
    uart_set_driver(&pl011_driver, (uintptr_t)uart_va);
    KDEBUG("UART remapped: 0x%08x -> %p", uart_get_base(), uart_va);
#else
    // Discover UART from DTB
    char uart_path[128];
    uint64_t uart_base, uart_size;

    if (dtb_find_compatible("arm,pl011", uart_path, sizeof(uart_path)))
    {
        if (dtb_get_reg_phys(uart_path, 0, &uart_base, &uart_size))
        {
            void *uart_va = ioremap((uintptr_t)uart_base, (size_t)uart_size);
            if (!uart_va)
            {
                KPANIC("Failed to ioremap UART");
            }
            uart_set_driver(&pl011_driver, (uintptr_t)uart_va);
            kprintf_init(uart_putc);
            KDEBUG("UART mapped: 0x%08x -> %p", (uint32_t)uart_base, uart_va);
        }
    }
    else
    {
        KPANIC("UART not found in DTB");
    }
#endif

    // Interrupts stay disabled until GIC is initialized

    uint64_t gicd_addr, gicd_size, gicc_addr, gicc_size;
    char gic_path[128];

    if (dtb_find_compatible("arm,cortex-a15-gic", gic_path, sizeof(gic_path)))
    {
        dtb_get_reg(gic_path, 0, &gicd_addr, &gicd_size);
        dtb_get_reg(gic_path, 1, &gicc_addr, &gicc_size);

        void *gicd_va = ioremap((uintptr_t)gicd_addr, (size_t)gicd_size);
        void *gicc_va = ioremap((uintptr_t)gicc_addr, (size_t)gicc_size);

        if (!gicd_va || !gicc_va)
        {
            KPANIC("Failed to ioremap GIC");
        }

        gic_init((uintptr_t)gicd_va, (uintptr_t)gicc_va);
        KDEBUG("GIC mapped: GICD @ 0x%x -> 0x%x", (uintptr_t)gicd_addr, gicd_va);
        KDEBUG("GIC mapped: GICC @ 0x%x -> 0x%x", (uintptr_t)gicc_addr, gicc_va);
    }
    else
    {
        KPANIC("GIC not found in DTB");
    }

    irq_init();
    pl011_init_irq(uart_get_base()); // Enable RX interrupts for UART

#ifndef SP804_TIMER
    generic_timer_init();
#else
    uint64_t sp804_addr, sp804_size;
    char sp804_path[128];
    if (dtb_find_compatible("arm,sp804", sp804_path, sizeof(sp804_path)))
    {
        if (dtb_get_reg_phys(sp804_path, 0, &sp804_addr, &sp804_size))
        {
            void *sp804_va = ioremap((uintptr_t)sp804_addr, (size_t)sp804_size);
            if (!sp804_va)
            {
                KPANIC("Failed to ioremap SP804");
            }
            sp804_init((uintptr_t)sp804_va, 10000);
            sp804_start((uintptr_t)sp804_va);
            KDEBUG("SP804 mapped: 0x%x -> %p", sp804_addr, sp804_va);
        }
    }
    else
    {
        generic_timer_init();
    }
#endif

    arch_global_irq_enable(); // Only after GIC is initialized

    KINFO("Booting...");

    // data abort test (page fault)
    // volatile uint32_t *bad = (volatile uint32_t *)0xDEADBEEF;
    //*bad = 42;

    print_boot_banner();

    // trigger SVC to verify exception handling
    // __asm__ volatile("svc #0");

    KINFO("Entering idle");
    while (1)
    {
        __asm__("wfi");
    }
}
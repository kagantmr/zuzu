#include "arch/arm/include/symbols.h"
#include "arch/arm/include/irq.h"
#include "arch/arm/include/gicv2.h"
#include "arch/arm/timer/generic_timer.h"

#include "arch/arm/mmu/mmu.h"
#include "arch/arm/vexpress-a15/board.h"

#include "drivers/uart/uart.h"
#include "drivers/uart/pl011.h"

#include "core/version.h"

#include "core/kprintf.h"
#include "core/panic.h"
#include <assert.h>

#include "kernel/layout.h"
#include "kernel/mm/pmm.h"
#include "kernel/dtb/dtb.h"
#include "kernel/mm/vmm.h"
#include "kernel/time/tick.h"
#include "kernel/sched/sched.h"
#include "kernel/proc/process.h"

#include "kernel/loader/initrd.h"
#include "kernel/loader/elf.h"
#include "kernel/loader/loader.h"
#include "kernel/loader/initrd.h"

#include <mem.h>
#include <string.h>
#include "kernel/mm/alloc.h"

#include "kernel/syspage.h"

#include "fetch.h"

#define LOG_FMT(fmt) "(main) " fmt
#include "core/log.h"


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

static process_t *s_devmgr; 

typedef struct boot_program {
    const char *path;
    uint32_t flags;
} boot_program_t;

static void inject_device_cap(const char *compatible,
                               uint64_t phys, uint64_t size,
                               uint32_t irq)
{
    if (!s_devmgr) return;
    device_cap_t *cap = kmalloc(sizeof(device_cap_t));
    if (!cap) return;
    strncpy(cap->compatible, compatible, sizeof(cap->compatible) - 1);
    cap->compatible[sizeof(cap->compatible) - 1] = '\0';
    cap->phys_base = (uint32_t)phys;
    cap->size = (uint32_t)size;
    cap->mapped = false;
    cap->irq = irq;
    // 3. handle_vec_find_free on s_devmgr->handle_table
    int handle = handle_vec_find_free(&s_devmgr->handle_table);
    if (handle < 0) {
        kfree(cap);
        return;
    }
    // 4. handle_vec_get that slot, write HANDLE_DEVICE entry
    handle_entry_t *entry = handle_vec_get(&s_devmgr->handle_table, (uint32_t)handle);
    if (!entry) {
        kfree(cap);
        return;
    }
    entry->type = HANDLE_DEVICE;
    entry->grantable = true;
    entry->dev = cap;
}

static void boot_program(const char *path, uint32_t flags)
{
    const void *elf_data;
    size_t elf_size;

    if (!initrd_find(path, &elf_data, &elf_size)) {
        KERROR("Missing boot program %s", path);
        return;
    }

    process_t *process = process_create_from_elf(elf_data, elf_size, path);
    if (!process) {
        KERROR("Failed to create boot program %s", path);
        return;
    }

    process->flags |= flags;

    if (flags & PROC_FLAG_DEVMGR) {
        s_devmgr = process;
        dtb_enum_devices(inject_device_cap);
    }

    sched_add(process);
}

static inline void perform_panic_tests(void)
{
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
    // __asm__ volatile("mov r0, #0x0\n" "str r0, [r0]\n");

    // test 7: a syscall should do absolutely nothing in SVC mode, used to crash, fixed by ignoring in SVC mode.
    //__asm__ volatile("svc #0");

    // test 8: just manually halt the system
    // panic("Test panic triggered (chewed wires)");
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
    if (!dtb_pa)
    {
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

    sched_init();
    // Keep IRQs globally masked during early boot setup and DTB enumeration.
    // User mode will run with its own CPSR after the first context switch.

    print_boot_banner();

    perform_panic_tests();

    syspage_init();

    initrd_init(_initrd_start, _initrd_end - _initrd_start);

    static const boot_program_t boot_programs[] = {
        {"bin/zuzusysd", PROC_FLAG_INIT},
        {"bin/devmgr", PROC_FLAG_DEVMGR},
        {"bin/zuart", 0},
        {"bin/zusd", 0},
        {"bin/fat32d", 0},
        {"bin/fbox", 0},
    };

    for (size_t i = 0; i < sizeof(boot_programs) / sizeof(boot_programs[0]); i++)
        boot_program(boot_programs[i].path, boot_programs[i].flags);


    /**
     * Hacky as hell I know but DTB enumeration is absolutely horrible with
     * IRQs enabled. Random panics happen left and right. It's not like we'll reach this section
     * ever again so I'm enabling interrupts as late as possible.
     */
    arch_global_irq_enable();

    register_tick_callback(set_resched_flag);

    // Kick off the first runnable userspace task; later preemption comes from timer ticks.
    KINFO("Entering idle");
    schedule();
    //uint64_t idle_ticks = 0;
    while (1)
    {
        sched_reap();
        arch_global_irq_enable();
        __asm__("wfi");
    }
}

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
#include <elf.h>
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
extern void arch_reboot(void);

#define BOOT_PROGRAM_PREFIX "bin/"

typedef struct boot_program {
    const char *path;
    uint32_t flags;
    uint8_t owns_path;
} boot_program_t;

static void inject_device_cap(const char *compatible,
                               uint64_t phys, uint64_t size,
                               uint32_t irq)
{
    if (!s_devmgr) return;
    device_cap_t *cap = (device_cap_t *)kalloc_device_cap();
    if (!cap) return;
    strncpy(cap->compatible, compatible, sizeof(cap->compatible) - 1);
    cap->compatible[sizeof(cap->compatible) - 1] = '\0';
    cap->phys_base = (uint32_t)phys;
    cap->size = (uint32_t)size;
    cap->mapped = false;
    cap->irq = irq;
    cap->ref_count = 1;
    // 3. handle_vec_find_free on s_devmgr->handle_table
    int handle = handle_vec_find_free(&s_devmgr->handle_table);
    if (handle < 0) {
        kfree_device_cap(cap);
        return;
    }
    // 4. handle_vec_get that slot, write HANDLE_DEVICE entry
    handle_entry_t *entry = handle_vec_get(&s_devmgr->handle_table, (uint32_t)handle);
    if (!entry) {
        kfree_device_cap(cap);
        return;
    }
    entry->type = HANDLE_DEVICE;
    entry->grantable = true;
    entry->mapped_va = 0;
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

    process_t *process = process_create_from_elf(elf_data, elf_size, path, NULL, 0, 0);
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

static uint32_t parse_flag_string(const char *flag_str)
{
    if (!flag_str)
        return 0;
    
    if (strcmp(flag_str, "init") == 0)
        return PROC_FLAG_INIT;
    if (strcmp(flag_str, "dev") == 0 || strcmp(flag_str, "devmgr") == 0)
        return PROC_FLAG_DEVMGR;
    if (strcmp(flag_str, "none") == 0)
        return 0;
    
    return 0;
}

static const char *normalize_manifest_program_path(const char *path_in)
{
    if (!path_in || !path_in[0])
        return NULL;

    if (strchr(path_in, '/')) {
        char *path = (char *)kmalloc(strlen(path_in) + 1);
        if (!path)
            return NULL;
        strcpy(path, path_in);
        return path;
    }

    size_t path_len = strlen(path_in);
    size_t full_len = sizeof(BOOT_PROGRAM_PREFIX) - 1 + path_len + 1;
    char *path = (char *)kmalloc(full_len);
    if (!path)
        return NULL;

    strcpy(path, BOOT_PROGRAM_PREFIX);
    strcpy(path + (sizeof(BOOT_PROGRAM_PREFIX) - 1), path_in);
    return path;
}

static size_t parse_boot_manifest(const char *manifest_data, size_t manifest_size,
                                   boot_program_t *out_programs, size_t max_programs)
{
    if (!manifest_data || !manifest_size || !out_programs || !max_programs)
        return 0;

    size_t count = 0;
    const char *line_start = manifest_data;
    const char *end = manifest_data + manifest_size;

    while (line_start < end && count < max_programs) {
        const char *line_end = line_start;
        while (line_end < end && *line_end != '\n')
            line_end++;

        size_t line_len = line_end - line_start;

        // skip empty lines and comments
        if (line_len == 0 || line_start[0] == '#' || line_start[0] == '\n') {
            line_start = line_end + 1;
            continue;
        }

        // trim trailing whitespace
        while (line_len > 0 && (line_start[line_len - 1] == '\r' || 
                                line_start[line_len - 1] == ' ' ||
                                line_start[line_len - 1] == '\t'))
            line_len--;

        // find pipe separator
        int pipe_idx = -1;
        for (size_t i = 0; i < line_len; i++) {
            if (line_start[i] == '|') {
                pipe_idx = i;
                break;
            }
        }

        if (pipe_idx <= 0) {
            KWARN("Boot manifest: invalid line format (missing pipe)");
            line_start = line_end + 1;
            continue;
        }

        // extract path
        const char *path_start = line_start;
        size_t path_len = pipe_idx;
        while (path_len > 0 &&
               (path_start[path_len - 1] == ' ' || path_start[path_len - 1] == '\t'))
            path_len--;
        char path_buf[256];
        if (path_len >= sizeof(path_buf)) {
            KWARN("Boot manifest: path too long");
            line_start = line_end + 1;
            continue;
        }
        memcpy(path_buf, path_start, path_len);
        path_buf[path_len] = '\0';

        // extract flags
        const char *flags_start = line_start + pipe_idx + 1;
        size_t flags_len = line_len - pipe_idx - 1;
        while (flags_len > 0 &&
               (*flags_start == ' ' || *flags_start == '\t')) {
            flags_start++;
            flags_len--;
        }
        while (flags_len > 0 &&
               (flags_start[flags_len - 1] == ' ' || flags_start[flags_len - 1] == '\t'))
            flags_len--;
        char flags_buf[64];
        if (flags_len >= sizeof(flags_buf)) {
            flags_len = sizeof(flags_buf) - 1;
        }
        memcpy(flags_buf, flags_start, flags_len);
        flags_buf[flags_len] = '\0';

        // populate output entry
        out_programs[count].path = normalize_manifest_program_path(path_buf);
        if (!out_programs[count].path) {
            KERROR("Boot manifest: allocation failed");
            break;
        }
        out_programs[count].flags = parse_flag_string(flags_buf);
        out_programs[count].owns_path = 1;

        count++;
        line_start = line_end + 1;
    }

    return count;
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
    panic("Test panic triggered (chewed wires)");
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

    // Read and parse boot manifest
    const void *manifest_data;
    size_t manifest_size;
    boot_program_t boot_programs[16];  // max 16 boot programs
    size_t boot_count = 0;

    if (initrd_find("boot.manifest", &manifest_data, &manifest_size)) {
        boot_count = parse_boot_manifest(manifest_data, manifest_size, 
                                         boot_programs, sizeof(boot_programs) / sizeof(boot_programs[0]));
        KDEBUG("Loaded boot manifest: %u programs", boot_count);
    } else {
        KWARN("Boot manifest not found in initrd");
        // Fallback to hardcoded defaults
        static const boot_program_t default_programs[] = {
            {"bin/zuzusysd", PROC_FLAG_INIT, 0},
            {"bin/devmgr", PROC_FLAG_DEVMGR, 0},
            {"bin/zuart", 0, 0},
            {"bin/zusd", 0, 0},
            {"bin/fat32d", 0, 0},
            {"bin/fbox", 0, 0},
        };
        boot_count = sizeof(default_programs) / sizeof(default_programs[0]);
        memcpy(boot_programs, default_programs, sizeof(default_programs));
    }

    // Spawn boot programs from manifest
    for (size_t i = 0; i < boot_count; i++) {
        boot_program(boot_programs[i].path, boot_programs[i].flags);
        if (boot_programs[i].owns_path && boot_programs[i].path) {
            kfree((void *)boot_programs[i].path);
            boot_programs[i].path = NULL;
            boot_programs[i].owns_path = 0;
        }
    }


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
        sched_idle_wait();
        schedule();
    }
}

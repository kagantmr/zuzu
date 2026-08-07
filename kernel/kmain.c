#include <arch/symbols.h>
#include <arch/cpu.h>

#include <arch/mmu.h>
#include <arch/platform.h>

#include "drivers/uart/uart.h"
#include "drivers/uart/pl011.h"

#include "core/panic.h"
#include "core/version.h"
#include <assert.h>

#include "kernel/layout.h"
#include "kernel/mm/pmm.h"
#include "boot_info.h"
#include "kernel/mm/vmm.h"
#include "kernel/time/tick.h"
#include "kernel/sched/sched.h"
#include "kernel/proc/process.h"

#include "kernel/loader/initrd.h"
#include <elf.h>
#include "kernel/loader/initrd.h"
#include <string.h>
#include "kernel/mm/alloc.h"

#include "kernel/syspage.h"
#include "zuzu/types.h"
#include <snprintf.h>
#include <zuzu/user_layout.h>


#define STR(x) #x
#define XSTR(x) STR(x)


#define LOG_FMT(fmt) "(main) " fmt
#include "core/log.h"

extern kernel_layout_t kernel_layout;
static inline uint32_t read_be32(const void *p)
{
    const uint8_t *b = (const uint8_t *)p;
    return ((uint32_t)b[0] << 24) |
           ((uint32_t)b[1] << 16) |
           ((uint32_t)b[2] << 8) |
           ((uint32_t)b[3]);
}

static ProcessObj *s_devmgr;

/* Set once in kmain(): the bootloader-supplied initrd (DTB /chosen), as a
 * physical address + size. */
static PhysAddr g_initrd_pa;
static size_t g_initrd_size;

#define BOOT_PROGRAM_PREFIX "bin/"

typedef struct boot_program
{
    const char *path;
    uint32_t flags;
    uint8_t owns_path;
} boot_program_t;

static void inject_device_cap(const char *compatible,
                              uint64_t phys, uint64_t size,
                              uint32_t irq)
{
    if (!s_devmgr)
        return;
    DeviceCap *cap = (DeviceCap *)kalloc_device_cap();
    if (!cap)
        return;
    strncpy(cap->compatible, compatible, sizeof(cap->compatible) - 1);
    cap->compatible[sizeof(cap->compatible) - 1] = '\0';
    cap->phys_base = (uint32_t)phys;
    cap->size = (uint32_t)size;
    cap->irq = irq;
    cap->ref_count = 1;
    // 3. handle_vec_find_free on s_devmgr->handle_table
    int handle = handle_vec_find_free(&s_devmgr->handle_table);
    if (handle < 0)
    {
        kfree_device_cap(cap);
        return;
    }
    // 4. handle_vec_get that slot, write HANDLE_DEVICE entry
    HandleEntry *entry = handle_vec_get(&s_devmgr->handle_table, (uint32_t)handle);
    if (!entry)
    {
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

    if (!initrd_find(path, &elf_data, &elf_size))
    {
        KERROR("Missing boot program %s", path);
        return;
    }

    /* PROC_FLAG_INIT gets the initrd mapped into its own address space
     * below. g_initrd_pa isn't necessarily page-aligned (a bootloader-
     * supplied ramdisk lands wherever it lands, e.g. u-boot's bootm skips
     * its own 64-byte legacy-image header, which is never page-sized), so
     * the mapping has to start at the containing page and the process
     * needs to be told exactly where the real data begins within it —
     * hence passing it via argv rather than a fixed/assumed address.
     * mmap_va_next always starts at USER_MMAP_BASE and process_create()
     * always reserves MAX_TCB_PAGES worth of TCB slots before returning,
     * so the mapping loop below is guaranteed to start at
     * USER_MMAP_BASE + MAX_TCB_PAGES * PAGE_SIZE. */
    char argbuf[320];
    size_t argbuf_len = 0;
    uint32_t argc = 0;
    uint32_t initrd_page_offset = 0, initrd_page_count = 0;
    uint32_t initrd_aligned_pa = 0;
    uintptr_t initrd_real_va = 0;

    if (flags & PROC_FLAG_INIT)
    {
        initrd_page_offset = g_initrd_pa & (PAGE_SIZE - 1);
        initrd_aligned_pa = g_initrd_pa - initrd_page_offset;
        initrd_page_count = (initrd_page_offset + (uint32_t)g_initrd_size + PAGE_SIZE - 1) / PAGE_SIZE;
        initrd_real_va = USER_MMAP_BASE + MAX_TCB_PAGES * PAGE_SIZE + initrd_page_offset;

        size_t off = 0;
        off += snprintf(argbuf + off, sizeof(argbuf) - off, "%s", path) + 1;
        off += snprintf(argbuf + off, sizeof(argbuf) - off, "%#x", (unsigned)initrd_real_va) + 1;
        off += snprintf(argbuf + off, sizeof(argbuf) - off, "%u", (unsigned)g_initrd_size) + 1;
        argbuf_len = off;
        argc = 3;
    }

    ProcessObj *process = KernelProcessLoad(elf_data, elf_size, path,
                                       argc ? argbuf : NULL, argbuf_len, argc);
    if (!process)
    {
        KERROR("Failed to create boot program %s", path);
        return;
    }

    process->flags |= flags;

    if (flags & PROC_FLAG_INIT)
    {
        for (uint32_t i = 0; i < initrd_page_count; i++)
        {
            uint32_t page_pa = initrd_aligned_pa + i * PAGE_SIZE;
            if (!VmmMapUserPage(process->as, page_pa, process->mmap_va_next, PROT_READ))
            {
                KERROR("Failed to map initrd page %u for %s", i, path);
                return;
            }
            process->mmap_va_next += PAGE_SIZE;
        }
        VmmAddRegion(process->as, &(VirtMemRegion){
                                        .vaddr_start = initrd_real_va,
                                        .size = g_initrd_size,
                                        .prot = PROT_READ | VM_PROT_USER,
                                        .memtype = VM_MEM_NORMAL,
                                        .owner = VM_OWNER_SHARED,
                                        .flags = VM_FLAG_NONE});
    }

    if (flags & PROC_FLAG_DEVMGR)
    {
        s_devmgr = process;
        boot_info_foreach_dev(inject_device_cap);
    }

    sched_add(process->thread);
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

    if (strchr(path_in, '/'))
    {
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

    while (line_start < end && count < max_programs)
    {
        const char *line_end = line_start;
        while (line_end < end && *line_end != '\n')
            line_end++;

        size_t line_len = line_end - line_start;

        // skip empty lines and comments
        if (line_len == 0 || line_start[0] == '#' || line_start[0] == '\n')
        {
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
        for (size_t i = 0; i < line_len; i++)
        {
            if (line_start[i] == '|')
            {
                pipe_idx = i;
                break;
            }
        }

        if (pipe_idx <= 0)
        {
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
        if (path_len >= sizeof(path_buf))
        {
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
               (*flags_start == ' ' || *flags_start == '\t'))
        {
            flags_start++;
            flags_len--;
        }
        while (flags_len > 0 &&
               (flags_start[flags_len - 1] == ' ' || flags_start[flags_len - 1] == '\t'))
            flags_len--;
        char flags_buf[64];
        if (flags_len >= sizeof(flags_buf))
        {
            flags_len = sizeof(flags_buf) - 1;
        }
        memcpy(flags_buf, flags_start, flags_len);
        flags_buf[flags_len] = '\0';

        // populate output entry
        out_programs[count].path = normalize_manifest_program_path(path_buf);
        if (!out_programs[count].path)
        {
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

/* register_tick_callback keeps a single slot (see kernel/time/tick.c), so
 * this wraps set_resched_flag rather than being registered alongside it —
 * a second call to register_tick_callback would silently replace the first
 * and stop preemption. */
static void sched_tick(void)
{
    set_resched_flag();
}

_Noreturn void kmain(void)
{
    KINFO("Booting %s", "zuzu-" ZUZU_CODENAME "-" ZUZU_VERSION);
    /* DTB and boot_info were initialized in early(); do not touch DTB again */

    sched_init();
    arch_global_irq_enable();

    SyspageInit();

    /* The initrd always comes from the bootloader/firmware now (u-boot's
     * bootm, or the Pi firmware on rpi4), via the DTB /chosen node. */
    uint64_t chosen_pa, chosen_size;
    if (!boot_info_initrd(&chosen_pa, &chosen_size))
        panic("No bootloader-supplied initrd (DTB /chosen)");

    g_initrd_pa = (PhysAddr)chosen_pa;
    g_initrd_size = (size_t)chosen_size;
    KINFO("initrd: bootloader-supplied at pa=%p size=%zu",
          (void *)(uintptr_t)g_initrd_pa, g_initrd_size);
    SyspageSetInitrdSz((uint32_t)g_initrd_size);

    initrd_init((const void *)PA_TO_VA((uintptr_t)g_initrd_pa), g_initrd_size);
    // Read and parse boot manifest
    const void *manifest_data;
    size_t manifest_size;
    boot_program_t boot_programs[16]; // max 16 boot programs
    size_t boot_count = 0;

    if (initrd_find("boot.manifest", &manifest_data, &manifest_size))
    {
        boot_count = parse_boot_manifest(manifest_data, manifest_size,
                                         boot_programs, sizeof(boot_programs) / sizeof(boot_programs[0]));
        KDEBUG("Loaded boot manifest: %u programs", boot_count);
    }
    else
    {
        panic("Boot manifest not found");
    }

    // Spawn boot programs from manifest
    for (size_t i = 0; i < boot_count; i++)
    {
        if (boot_programs[i].flags & (PROC_FLAG_INIT | PROC_FLAG_DEVMGR))
            boot_program(boot_programs[i].path, boot_programs[i].flags);
        if (boot_programs[i].owns_path && boot_programs[i].path)
        {
            kfree((void *)boot_programs[i].path);
            boot_programs[i].path = NULL;
            boot_programs[i].owns_path = 0;
        }
    }

    register_tick_callback(sched_tick);

    KINFO("Entering idle");


    schedule();
    
    panic("Unreachable: %s:%d", __FILE__, __LINE__);
}

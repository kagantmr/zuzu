#include "arch/arm/include/symbols.h"
#include "arch/arm/include/irq.h"

#include "arch/arm/mmu/mmu.h"
#include "arch/arm/vexpress-a15/board.h"

#include "drivers/uart/uart.h"
#include "drivers/uart/pl011.h"

#include "core/version.h"

#include "core/kprintf.h"
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
#include <mem.h>
#include <string.h>
#include "kernel/mm/alloc.h"

#include "kernel/syspage.h"


#define STR(x) #x
#define XSTR(x) STR(x)


#define LOG_FMT(fmt) "(main) " fmt
#include "core/log.h"

extern kernel_layout_t kernel_layout;
extern pmm_state_t pmm_state;
static inline uint32_t read_be32(const void *p)
{
    const uint8_t *b = (const uint8_t *)p;
    return ((uint32_t)b[0] << 24) |
           ((uint32_t)b[1] << 16) |
           ((uint32_t)b[2] << 8) |
           ((uint32_t)b[3]);
}

static process_t *s_devmgr;

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
    device_cap_t *cap = (device_cap_t *)kalloc_device_cap();
    if (!cap)
        return;
    strncpy(cap->compatible, compatible, sizeof(cap->compatible) - 1);
    cap->compatible[sizeof(cap->compatible) - 1] = '\0';
    cap->phys_base = (uint32_t)phys;
    cap->size = (uint32_t)size;
    cap->mapped = false;
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
    handle_entry_t *entry = handle_vec_get(&s_devmgr->handle_table, (uint32_t)handle);
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

    process_t *process = process_load(elf_data, elf_size, path, NULL, 0, 0);
    if (!process)
    {
        KERROR("Failed to create boot program %s", path);
        return;
    }

    process->flags |= flags;

    if (flags & PROC_FLAG_INIT)
    {
        uint32_t initrd_pa = VA_TO_PA(_initrd_start);
        uint32_t initrd_page_count = (_initrd_end - _initrd_start + PAGE_SIZE - 1) / PAGE_SIZE;
        uintptr_t va = process->mmap_va_next;
        for (uint32_t i = 0; i < initrd_page_count; i++)
        {
            uint32_t page_pa = initrd_pa + i * PAGE_SIZE;
            vmm_map_user_page(process->as, page_pa, process->mmap_va_next, VM_PROT_READ);
            process->mmap_va_next += PAGE_SIZE;
        }
        vmm_add_region(process->as, &(vm_region_t){
                                        .vaddr_start = va,
                                        .size = _initrd_end - _initrd_start,
                                        .prot = VM_PROT_READ | VM_PROT_USER,
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

_Noreturn void kmain(void)
{
    KINFO("Booting zuzu version %s", ZUZU_VERSION);
    /* DTB and boot_info were initialized in early(); do not touch DTB again */

    sched_init();
    arch_global_irq_enable();

    syspage_init();
    syspage_set_initrd_size((uint32_t)(_initrd_end - _initrd_start));

    initrd_init(_initrd_start, _initrd_end - _initrd_start);
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

    register_tick_callback(set_resched_flag);

    KINFO("Entering idle");


    schedule();
    
    panic("Unreachable: " __FILE__ ":" XSTR(__LINE__));
}

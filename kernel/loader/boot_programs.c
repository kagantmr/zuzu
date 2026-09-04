#include <arch/mmu.h>
#include <arch/platform.h>

#include "core/panic.h"
#include "kernel/boot_info.h"
#include "kernel/mm/vmm.h"
#include "kernel/sched/sched.h"
#include "kernel/proc/process.h"

#include "kernel/loader/initrd.h"
#include "kernel/loader/boot_programs.h"
#include <zuzu/zxf.h>
#include <string.h>
#include "kernel/mm/alloc.h"
#include "zuzu/types.h"
#include <snprintf.h>
#include <zuzu/boot.h>
#include <zuzu/user_layout.h>

#define LOG_FMT(fmt) "(loader) " fmt
#include "core/log.h"

static ProcessObj *s_devmgr;
static ProcessObj *s_sysd;

/* Set once in boot_programs_spawn_all(): the bootloader-supplied initrd
 * (DTB /chosen), as a physical address + size. */
static PhysAddr g_initrd_pa;
static size_t g_initrd_size;

#define BOOT_PROGRAM_PREFIX "bin/"

typedef struct boot_program
{
    char *path;
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

/* devmgr's entry point/sp as computed by a parse-only peek at its ELF
 * before sysd is created (see boot_programs_spawn_all()) — passed through
 * so sysd's own argv (PROC_FLAG_INIT branch below) can carry them. devmgr
 * is loaded for real, FROZEN, later in the same boot_programs[] loop; sysd
 * later reads these same values back out of its argv to SysKickstart
 * devmgr once it has granted it what it needs. */
static void boot_program(const char *path, uint32_t flags,
                         uint32_t devmgr_entry_peek, uint32_t devmgr_sp_peek)
{
    const void *zxf_data;
    size_t zxf_size;

    if (!initrd_find(path, &zxf_data, &zxf_size))
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
        off += (size_t)snprintf(argbuf + off, sizeof(argbuf) - off, "%s", path) + 1;
        off += (size_t)snprintf(argbuf + off, sizeof(argbuf) - off, "%#x", (unsigned)initrd_real_va) + 1;
        off += (size_t)snprintf(argbuf + off, sizeof(argbuf) - off, "%u", (unsigned)g_initrd_size) + 1;
        off += (size_t)snprintf(argbuf + off, sizeof(argbuf) - off, "%#x", (unsigned)devmgr_entry_peek) + 1;
        off += (size_t)snprintf(argbuf + off, sizeof(argbuf) - off, "%#x", (unsigned)devmgr_sp_peek) + 1;
        argbuf_len = off;
        argc = 5;
    }

    bool leave_frozen = (flags & PROC_FLAG_DEVMGR) != 0;
    ProcessObj *process = KernelProcessLoad(zxf_data, zxf_size, path,
                                       argc ? argbuf : NULL, argbuf_len, argc,
                                       leave_frozen);
    if (!process)
    {
        KERROR("Failed to create boot program %s", path);
        return;
    }

    process->flags |= flags;

    if (flags & PROC_FLAG_INIT)
    {
        s_sysd = process;
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

    /* devmgr stays FROZEN until sysd grants it what it needs and
     * SysKickstarts it (which calls sched_add itself once it flips the
     * thread to READY) — scheduling it now would run it with no valid
     * trap frame. */
    if (!leave_frozen)
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

static char *normalize_manifest_program_path(const char *path_in)
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

        size_t line_len = (size_t)(line_end - line_start);

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
                pipe_idx = (int)i;
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
        size_t path_len = (size_t)pipe_idx;
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
        size_t flags_len = line_len - (size_t)pipe_idx - 1;
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

void boot_programs_spawn_all(PhysAddr initrd_pa, size_t initrd_size)
{
    g_initrd_pa = initrd_pa;
    g_initrd_size = initrd_size;

    // Read and parse boot manifest
    const void *manifest_data;
    size_t manifest_size;
    /* static, not a stack local: boot_programs_spawn_all() runs exactly
     * once at boot (see kmain.c), so this doesn't need per-call storage --
     * moving it off the stack keeps this function's frame under the
     * 512-byte budget. */
    static boot_program_t boot_programs[16]; // max 16 boot programs
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
    uint32_t devmgr_entry_peek = 0;
    uint32_t devmgr_sp_peek = 0;
    for (size_t i = 0; i < boot_count; i++)
    {
        if (!(boot_programs[i].flags & PROC_FLAG_DEVMGR))
            continue;
        const void *devmgr_zxf_data;
        size_t devmgr_zxf_size;
        if (!initrd_find(boot_programs[i].path, &devmgr_zxf_data, &devmgr_zxf_size))
        {
            KERROR("Missing boot program %s", boot_programs[i].path);
            break;
        }
        ZXFImage img;
        if (ZxfParse(devmgr_zxf_data, devmgr_zxf_size, &img) == false) {
            panic("Could not parse devmgr");
        }
        devmgr_entry_peek = img.entry;
        devmgr_sp_peek = USR_SP;
        break;
    }

    // Spawn boot programs from manifest
    for (size_t i = 0; i < boot_count; i++)
    {
        if (boot_programs[i].flags & (PROC_FLAG_INIT | PROC_FLAG_DEVMGR))
            boot_program(boot_programs[i].path, boot_programs[i].flags,
                         devmgr_entry_peek, devmgr_sp_peek);
        if (boot_programs[i].owns_path && boot_programs[i].path)
        {
            kfree((void *)boot_programs[i].path);
            boot_programs[i].path = NULL;
            boot_programs[i].owns_path = 0;
        }
    }

    /* Seed devmgr's task handle directly into sysd's own handle table, at a
     * fixed slot sysd's userspace code already knows by constant, so sysd
     * can SysKickstart devmgr without ever calling SysPSpawn for it. Same
     * direct-write pattern as inject_device_cap() above, just at a fixed
     * slot instead of one returned by handle_vec_find_free. */
    if (s_sysd && s_devmgr)
    {
        HandleEntry *devmgr_task_slot =
            handle_vec_get(&s_sysd->handle_table, SYSD_DEVMGR_TASK_HANDLE_SLOT);
        if (devmgr_task_slot)
        {
            devmgr_task_slot->type = HANDLE_TASK;
            devmgr_task_slot->grantable = true;
            devmgr_task_slot->mapped_va = 0;
            devmgr_task_slot->task = s_devmgr;
        }
        else
        {
            KERROR("Failed to seed devmgr task handle into sysd's handle table");
        }
    }
}

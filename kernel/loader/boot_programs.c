#include <arch/mmu.h>
#include <arch/platform.h>

#include "core/panic.h"
#include "kernel/boot_info.h"
#include "kernel/mm/vmm/vmm.h"
#include "kernel/sched/sched.h"
#include "kernel/space/space.h"
#include "kernel/task/kernel_load.h"

#include "kernel/loader/boot_programs.h"
#include "kernel/loader/initrd.h"
#include "kernel/mm/alloc.h"
#include "types.h"
#include <snprintf.h>
#include <string.h>
#include <util/zxf.h>
#include <zuzu/bootinfo.h>
#include <zuzu/user_layout.h>

#define LOG_FMT(fmt) "(loader) " fmt
#include "core/log.h"

static SpaceObject *s_rootsvc;

static PhysAddr g_initrd_pa;
static size_t g_initrd_size;

#define BOOT_PROGRAM_PREFIX "bin/"

static void InjectDeviceObjects(const char *compatible, uint64_t phys, uint64_t size, uint32_t irq)
{
    (void)compatible;
    static int32_t index = 0;
    if (!s_rootsvc)
        return;
    HandleTableEntry *entry =
        HandleTableGetOrAlloc(&s_rootsvc->handle_table, (Handle)DEVICE_HANDLE_BASE + index++);
    if (!entry)
        return;
    MemObject *mem = MemObjCreateDevice((PhysAddr)phys, (size_t)size, irq);
    if (!mem)
        return;

    entry->type = HANDLE_MEM;
    entry->mem = mem;
    entry->perms = PERM_ALL;
    entry->mapped_va = 0;
    HandleEntryClaim(&s_rootsvc->handle_table, entry);
}

static void CreateRootSpace(const char *path)
{
    const void *zxf_data;
    size_t zxf_size;

    if (!initrd_find(path, &zxf_data, &zxf_size))
    {
        KERROR("Missing boot program %s", path);
        return;
    }

    SpaceObject *space = KernelSpaceLoad(zxf_data, zxf_size, path, NULL, 0, 0, false);
    if (!space)
    {
        KERROR("Failed to create boot program %s", path);
        return;
    }
    s_rootsvc = space;
    BootInfoEnumerateDevs(InjectDeviceObjects);

    uint32_t initrd_page_offset = g_initrd_pa & (PAGE_SIZE - 1);
    uint32_t initrd_aligned_pa = g_initrd_pa - initrd_page_offset;
    uint32_t initrd_page_count =
        (initrd_page_offset + (uint32_t)g_initrd_size + PAGE_SIZE - 1) / PAGE_SIZE;
    uintptr_t initrd_base_va = USER_MMAP_BASE + (MAX_TCB_PAGES * PAGE_SIZE);

    for (uint32_t i = 0; i < initrd_page_count; i++)
    {
        uint32_t page_pa = initrd_aligned_pa + (i * PAGE_SIZE);
        if (!VmmMapUserPage(space->as, page_pa, initrd_base_va + (i * PAGE_SIZE), PROT_READ))
        {
            KERROR("Failed to map initrd page %u for %s", i, path);
            return;
        }
    }
    VmmAddRegion(space->as, &(VirtMemRegion){.vaddr_start = initrd_base_va,
                                               .size = initrd_page_count * PAGE_SIZE,
                                               .prot = PROT_READ | VM_PROT_USER,
                                               .memtype = VM_MEM_NORMAL,
                                               .owner = VM_OWNER_SHARED,
                                               .flags = VM_FLAG_NONE});

    size_t bootinfo_pages = (sizeof(BootInfo) + PAGE_SIZE - 1) / PAGE_SIZE;
    for (size_t i = 0; i < bootinfo_pages; i++)
    {
        if (!VmmMapUserPage(space->as, BootInfoPa() + (i * PAGE_SIZE),
                            USER_BOOTINFO_VA + (i * PAGE_SIZE), PROT_READ))
        {
            KERROR("Failed to map boot info page %zu for %s", i, path);
            return;
        }
    }
    VmmAddRegion(space->as, &(VirtMemRegion){.vaddr_start = USER_BOOTINFO_VA,
                                               .size = bootinfo_pages * PAGE_SIZE,
                                               .prot = PROT_READ | VM_PROT_USER,
                                               .memtype = VM_MEM_NORMAL,
                                               .owner = VM_OWNER_SHARED,
                                               .flags = VM_FLAG_NONE});

    SchedAdd(space->main_task);
}

static char *NormalizeManifestPath(const char *path_in)
{
    if (!path_in || !path_in[0])
        return NULL;

    if (strchr(path_in, '/'))
    {
        char *path = (char *)KZAlloc(strlen(path_in) + 1);
        if (!path)
            return NULL;
        strcpy(path, path_in);
        return path;
    }

    size_t path_len = strlen(path_in);
    size_t full_len = sizeof(BOOT_PROGRAM_PREFIX) - 1 + path_len + 1;
    char *path = (char *)KZAlloc(full_len);
    if (!path)
        return NULL;

    strcpy(path, BOOT_PROGRAM_PREFIX);
    strcpy(path + (sizeof(BOOT_PROGRAM_PREFIX) - 1), path_in);
    return path;
}

static char *FindRootsvcPath(const char *manifest_data, size_t manifest_size)
{
    const char *line_start = manifest_data;
    const char *end = manifest_data + manifest_size;

    while (line_start < end)
    {
        const char *line_end = line_start;
        while (line_end < end && *line_end != '\n')
            line_end++;

        size_t line_len = (size_t)(line_end - line_start);
        while (line_len > 0 &&
               (line_start[line_len - 1] == '\r' || line_start[line_len - 1] == ' ' ||
                line_start[line_len - 1] == '\t'))
            line_len--;

        if (line_len > 0 && line_start[0] != '#')
        {
            char path_buf[256];
            if (line_len >= sizeof(path_buf))
                return NULL;
            memcpy(path_buf, line_start, line_len);
            path_buf[line_len] = '\0';
            return NormalizeManifestPath(path_buf);
        }

        line_start = line_end + 1;
    }
    return NULL;
}

void CreateRootService(PhysAddr initrd_pa, size_t initrd_size)
{
    g_initrd_pa = initrd_pa;
    g_initrd_size = initrd_size;

    const void *manifest_data;
    size_t manifest_size;
    if (!initrd_find("boot.manifest", &manifest_data, &manifest_size))
        panic("Boot manifest not found");

    char *rootsvc_path = FindRootsvcPath(manifest_data, manifest_size);
    if (!rootsvc_path)
        panic("Boot manifest has no rootsvc entry");

    CreateRootSpace(rootsvc_path);
    KFree(rootsvc_path);
}

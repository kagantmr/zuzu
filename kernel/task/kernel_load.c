#include "kernel_load.h"

#include "core/panic.h"
#include "core/ensure.h"

#include "kernel/mm/alloc.h"
#include "kernel/mm/pmm/pmm.h"
#include "kernel/task/task.h"

#include <arch/cache.h>
#include <arch/context.h>
#include <arch/mmu.h>

#include <elf.h>
#include <stdint.h>
#include <string.h>

#include <zuzu/err.h>
#include <zuzu/user_layout.h>
#include <zuzu/zxf.h>

#define LOG_FMT(fmt) "(kload) " fmt
#include "core/log.h"

static bool ZxfSegChkOverlap(const ZXFSegment *a, const ZXFSegment *b)
{
    uint32_t a_start = (uint32_t)a->vaddr;
    uint32_t b_start = (uint32_t)b->vaddr;
    uint32_t a_end = (uint32_t)a->vaddr + (uint32_t)a->mem_size;
    uint32_t b_end = (uint32_t)b->vaddr + (uint32_t)b->mem_size;

    if (a_end < a_start || b_end < b_start)
        return true;

    return (a_start < b_end) && (b_start < a_end);
}

/* Copy into another address space through the kernel alias of each page.
 * The target pages must already be faulted in (see fault_in_pages). */
static bool AddrSpaceCopyOut(AddressSpace *as, VirtAddr va, const void *src, size_t len)
{
    const uint8_t *s = src;
    while (len > 0)
    {
        VirtAddr page_va = va & ~(VirtAddr)(PAGE_SIZE - 1);
        PhysAddr pa = ArchMmuTranslate(as->pt_root_physaddr, page_va);
        if (pa == 0)
            return false;
        size_t off = va - page_va;
        size_t n = PAGE_SIZE - off;
        if (n > len)
            n = len;
        memcpy((void *)(PA_TO_VA(pa) + off), s, n);
        s += n;
        va += n;
        len -= n;
    }
    return true;
}

/* On failure after Space+Task were both created: torn_down teardown of
 * everything the space owns, then free the (never-launched) task itself,
 * which in turn frees the now-empty space via SpaceFinalize. */
static void KernelLoadFail(SpaceObject *sp, TaskObject *t)
{
    SpaceDestroy(sp);
    TaskDestroy(t);
}

SpaceObject *KernelProcessLoad(const void *zxf_data, size_t zxf_size, const char *name,
                                const char *argbuf, size_t argbuf_len, uint32_t argc,
                                bool leave_frozen)
{
    ZXFImage img;
    bool valid = ZxfParse(zxf_data, zxf_size, &img);
    if (!valid)
        return NULL;

    SpaceObject *p = SpaceCreate(name);
    if (!p)
        return NULL;
    TaskObject *t = TaskCreate(p);
    if (!t)
    {
        /* p->tasks is still empty here, so SpaceDestroy's own finalize
         * check frees p immediately -- no separate SpaceFinalize call. */
        SpaceDestroy(p);
        return NULL;
    }
    VirtAddr stack_top = t->kernel_stack_top;

    for (int i = 0; i < img.seg_count; i++)
    {
        const ZXFSegment *seg_i = &img.segs[i];

        for (int j = i + 1; j < img.seg_count; j++)
        {
            const ZXFSegment *seg_j = &img.segs[j];

            if (ZxfSegChkOverlap(seg_i, seg_j))
            {
                KERROR("ELF load segments overlap: [%08X, %08X) and [%08X, %08X)", seg_i->vaddr,
                       seg_i->vaddr + seg_i->mem_size, seg_j->vaddr,
                       seg_j->vaddr + seg_j->mem_size);
                KernelLoadFail(p, t);
                return NULL;
            }
        }
    }

    for (int i = 0; i < img.seg_count; i++)
    {
        const ZXFSegment *seg = &img.segs[i];
        if (seg->file_offset + seg->file_size > zxf_size)
        {
            KernelLoadFail(p, t);
            return NULL;
        }
        /* [filesz, memsz) is BSS: zero by definition, with no file
         * content behind it. Only [0, page_align_up(filesz)) needs an
         * eager alloc+copy; the rest is registered as anon and faults
         * in lazily via vmm_fault_page(), same as the stack reserve. */
        size_t file_pages = (seg->file_size + PAGE_SIZE - 1) / PAGE_SIZE;
        size_t mem_pages = (seg->mem_size + PAGE_SIZE - 1) / PAGE_SIZE;

        uintptr_t *segment_pages = NULL;
        if (file_pages > 0)
        {
            segment_pages = KMalloc(file_pages * sizeof(uintptr_t));
            if (!segment_pages)
            {
                KernelLoadFail(p, t);
                return NULL;
            }
        }

        uint32_t prot = 0;
        if (seg->flags & ZXF_R)
            prot |= PROT_READ;
        if (seg->flags & ZXF_W)
            prot |= PROT_WRITE;
        if (seg->flags & ZXF_X)
            prot |= PROT_EXEC;

        for (uint32_t page = 0; page < file_pages; page++)
        {
            uintptr_t page_pa = PmmAllocFrame();
            if (!page_pa)
            {
                for (uint32_t j = 0; j < page; j++)
                {
                    uintptr_t orphan_va = (uint32_t)seg->vaddr + (j * PAGE_SIZE);
                    VmmUnmapRange(p->as, orphan_va, PAGE_SIZE, true);
                    PmmFreeFrame(segment_pages[j]);
                }
                KFree(segment_pages);
                KernelLoadFail(p, t);
                return NULL;
            }

            segment_pages[page] = page_pa;

            /* Every page here is < file_pages, so file_offset < p_filesz
             * always holds; the page containing p_filesz (the boundary
             * page) is part file content, part BSS, so the tail past
             * p_filesz must be explicitly zeroed - PmmAllocPage() can
             * return a recycled frame with arbitrary contents. */
            VirtAddr file_offset = page * PAGE_SIZE;
            size_t bytes_to_copy = seg->file_size - file_offset;
            if (bytes_to_copy > PAGE_SIZE)
                bytes_to_copy = PAGE_SIZE;

            memcpy((void *)PA_TO_VA(page_pa),
                   (const uint8_t *)zxf_data + seg->file_offset + file_offset, bytes_to_copy);

            if (bytes_to_copy < PAGE_SIZE)
            {
                memset((uint8_t *)PA_TO_VA(page_pa) + bytes_to_copy, 0, PAGE_SIZE - bytes_to_copy);
            }

            VirtAddr va = (uint32_t)seg->vaddr + (page * PAGE_SIZE);
            if (!VmmMapUserPage(p->as, page_pa, va, prot))
            {
                PmmFreeFrame(page_pa);
                for (uint32_t j = 0; j < page; j++)
                {
                    VirtAddr orphan_va = (uint32_t)seg->vaddr + (j * PAGE_SIZE);
                    VmmUnmapRange(p->as, orphan_va, PAGE_SIZE, true);
                    PmmFreeFrame(segment_pages[j]);
                }
                KFree(segment_pages);
                KernelLoadFail(p, t);
                return NULL;
            }
        }

        /* Publish the code through the kernel alias, not seg->vaddr: that is a
         * VA in p->as, which is not the active translation regime here, and
         * cache maintenance by VA takes a translation fault when the address
         * does not translate (DFSR.CM). QEMU does not model that, so this only
         * ever showed up on real hardware. The I-cache is invalidated whole
         * rather than by VA for the same reason. */
        if ((prot & PROT_EXEC) && file_pages > 0)
        {
            for (uint32_t page = 0; page < file_pages; page++)
                arch_cache_clean_dcache_range(PA_TO_VA(segment_pages[page]), PAGE_SIZE);
            arch_cache_invalidate_icache_all();
        }

        if (file_pages > 0)
        {
            VirtMemRegion seg_region = {
                .vaddr_start = (uint32_t)seg->vaddr,
                .size = file_pages * PAGE_SIZE,
                .prot = prot | VM_PROT_USER,
                .memtype = VM_MEM_NORMAL,
                .owner = VM_OWNER_ANON,
                .flags = VM_FLAG_NONE,
            };
            if (!VmmAddRegion(p->as, &seg_region))
            {
                KERROR("Failed to add ELF segment region at VA %08X", (uint32_t)seg->vaddr);
                for (uint32_t j = 0; j < file_pages; j++)
                {
                    VirtAddr orphan_va = (uint32_t)seg->vaddr + (j * PAGE_SIZE);
                    VmmUnmapRange(p->as, orphan_va, PAGE_SIZE, true);
                    PmmFreeFrame(segment_pages[j]);
                }
                KFree(segment_pages);
                KernelLoadFail(p, t);
                return NULL;
            }

            KFree(segment_pages);
        }

        if (mem_pages > file_pages)
        {
            VirtMemRegion bss_region = {
                .vaddr_start = (uint32_t)seg->vaddr + (file_pages * PAGE_SIZE),
                .size = (mem_pages - file_pages) * PAGE_SIZE,
                .prot = prot | VM_PROT_USER,
                .memtype = VM_MEM_NORMAL,
                .owner = VM_OWNER_ANON,
                .flags = VM_FLAG_NONE,
            };
            if (!VmmAddRegion(p->as, &bss_region))
            {
                KERROR("Failed to add BSS region at VA %08X", bss_region.vaddr_start);
                KernelLoadFail(p, t);
                return NULL;
            }
        }
    }

    /* Stack region + guard were reserved by SpaceCreate; pages fault
     * in on demand. */
    const VirtAddr user_stack_base = USER_STACK_BASE;

    VirtAddr sp = USR_SP;
    VirtAddr argv_va = 0;

    if ((argc > 0) != (argbuf && argbuf_len > 0))
    {
        KERROR("Invalid argv payload: argc=%u argbuf_len=%u", argc, (unsigned)argbuf_len);
        KernelLoadFail(p, t);
        return NULL;
    }

    if (argbuf && argbuf_len > 0 && argc > 0)
    {
        if ((argbuf)[argbuf_len - 1] != '\0')
        {
            KERROR("Invalid argv payload: missing trailing NUL");
            KernelLoadFail(p, t);
            return NULL;
        }

        size_t nul_count = 0;
        for (size_t i = 0; i < argbuf_len; i++)
        {
            if ((argbuf)[i] == '\0')
            {
                nul_count++;
            }
        }
        if (nul_count < argc)
        {
            KERROR("Invalid argv payload: argc exceeds NUL-delimited strings");
            KernelLoadFail(p, t);
            return NULL;
        }

        size_t argv_slots = (size_t)argc + 1U;
        if (argv_slots <= (size_t)argc)
        {
            KERROR("Invalid argv payload: argc too large");
            KernelLoadFail(p, t);
            return NULL;
        }

        size_t argv_bytes = argv_slots * sizeof(uint32_t);
        if (argv_bytes / sizeof(uint32_t) != argv_slots)
        {
            KERROR("Invalid argv payload: argv bytes overflow");
            KernelLoadFail(p, t);
            return NULL;
        }
        VirtAddr check_sp = USR_SP;

        if (argbuf_len > (size_t)(check_sp - user_stack_base))
        {
            KERROR("argv payload does not fit user stack");
            KernelLoadFail(p, t);
            return NULL;
        }

        check_sp -= argbuf_len;
        check_sp &= ~((uintptr_t)3U);

        if (argv_bytes > (size_t)(check_sp - user_stack_base))
        {
            KERROR("argv pointer array does not fit user stack");
            KernelLoadFail(p, t);
            return NULL;
        }

        check_sp -= argv_bytes;
        check_sp &= ~((uintptr_t)7U);

        if (check_sp < user_stack_base)
        {
            KERROR("argv layout underflowed user stack");
            KernelLoadFail(p, t);
            return NULL;
        }

        sp -= argbuf_len;
        sp &= ~3U;
        VirtAddr strings_va = sp;

        sp -= (argc + 1) * sizeof(uint32_t);
        sp &= ~7U;
        argv_va = sp;

        /* Fault in the stack pages the argv block spans, then write them
         * through the kernel alias (the target AS is not active here). */
        if (!VmmCheckUserFault(p->as, argv_va, (size_t)(USR_SP - argv_va), true))
        {
            KERROR("failed to fault in argv stack pages");
            KernelLoadFail(p, t);
            return NULL;
        }

        if (!AddrSpaceCopyOut(p->as, strings_va, argbuf, argbuf_len))
        {
            KernelLoadFail(p, t);
            return NULL;
        }

        /* String offsets in the target stack mirror offsets in argbuf. */
        VirtAddr str_va = strings_va;
        const char *str_src = argbuf;
        for (uint32_t a = 0; a <= argc; a++)
        {
            uint32_t slot = (a < argc) ? (uint32_t)str_va : 0;
            if (!AddrSpaceCopyOut(p->as, argv_va + (a * sizeof(uint32_t)), &slot, sizeof(slot)))
            {
                KernelLoadFail(p, t);
                return NULL;
            }
            if (a < argc)
            {
                size_t l = strlen(str_src) + 1;
                str_va += l;
                str_src += l;
            }
        }
    }

    if (!leave_frozen)
    {
        t->kernel_sp = (uint32_t *)arch_thread_user_init(
            (void *)stack_top, (uintptr_t)img.entry, (uintptr_t)sp, USER_ELF_BASE, argc,
            (uint32_t)argv_va, &t->trap_frame);
        t->state = READY;
    }
    /* leave_frozen: task stays FROZEN (TaskCreate's default) with no
     * trap frame set up yet. The caller is expected to SysKickstart this
     * task later, which performs the deferred arch_thread_user_init
     * call with the entry/sp it supplies at that time. */

    KTRACE("space create: pid=%d name=%s tid=%u owner_task=%p as=%p", p->pid, p->name,
           t->tid, (void *)t, (void *)p->as);
    return p;
}
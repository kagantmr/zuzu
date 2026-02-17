#include "arch/arm/include/symbols.h"
#include "arch/arm/include/irq.h"

#include "arch/arm/vexpress-a15/board.h"
#include "arch/arm/mmu/mmu.h"

#include "kernel/layout.h"
#include "kernel/mm/pmm.h"
#include "kernel/kmain.h"
#include "kernel/dtb/dtb.h"
#include "kernel/mm/alloc.h"
#include "kernel/vmm/vmm.h"

#include "core/assert.h"
#include "core/log.h"
#include "core/panic.h"

#include "lib/mem.h"
#include "lib/string.h"

#include "drivers/uart/uart.h"
#include "drivers/uart/pl011.h"

extern kernel_layout_t kernel_layout;
extern pmm_state_t pmm_state;
extern phys_region_t phys_region;
extern addrspace_t *g_kernel_as;

/* =============================================================================
 * Early Boot Page Table (placed in .bss.boot, physical address space)
 *
 * L1 table for short-descriptor format:
 *   - 4096 entries × 4 bytes = 16KB
 *   - Each entry maps 1MB section
 *   - Must be 16KB aligned
 * ============================================================================= */
__attribute__((section(".bss.boot"), aligned(16384))) uint32_t early_l1[4096];

/* =============================================================================
 * ARMv7 Short-Descriptor L1 Section Entry Format
 *
 * Bits [31:20] = Section base address (PA[31:20])
 * Bits [19]    = NS (Non-Secure)
 * Bits [18]    = 0
 * Bits [17]    = nG (not Global) - 0 for kernel
 * Bits [16]    = S (Shareable)
 * Bits [15]    = AP[2]
 * Bits [14:12] = TEX[2:0]
 * Bits [11:10] = AP[1:0]
 * Bits [9]     = Implementation defined
 * Bits [8:5]   = Domain
 * Bits [4]     = XN (Execute Never)
 * Bits [3]     = C (Cacheable)
 * Bits [2]     = B (Bufferable)
 * Bits [1:0]   = 0b10 for Section descriptor
 * ============================================================================= */

/* Section descriptor type */
#define L1_TYPE_SECTION 0x2

/* Access permissions: AP[2:0] = 0b011 = full access (privileged R/W, user R/W) */
#define L1_AP_FULL_ACCESS ((0 << 15) | (3 << 10)) /* AP[2]=0, AP[1:0]=11 */

/* Memory attributes for normal memory (cacheable, write-back, write-allocate) */
/* TEX=001, C=1, B=1 -> Outer and Inner Write-Back, Write-Allocate */
#define L1_MEM_NORMAL ((1 << 12) | (1 << 3) | (1 << 2)) /* TEX=001, C=1, B=1 */

/* Memory attributes for device memory (strongly ordered / device) */
/* TEX=000, C=0, B=1 -> Shared Device memory */
#define L1_MEM_DEVICE ((0 << 12) | (0 << 3) | (1 << 2)) /* TEX=000, C=0, B=1 */

/* Shareable bit */
#define L1_SHAREABLE (1 << 16)

/* Execute Never bit */
#define L1_XN (1 << 4)

/* Domain 0 */
#define L1_DOMAIN_0 (0 << 5)

/* Build a section entry for normal memory (cacheable, executable) */
#define SECTION_NORMAL(pa)                                       \
    (((pa) & 0xFFF00000) | L1_TYPE_SECTION | L1_AP_FULL_ACCESS | \
     L1_MEM_NORMAL | L1_SHAREABLE | L1_DOMAIN_0)

/* Build a section entry for device memory (non-cacheable, non-executable) */
#define SECTION_DEVICE(pa)                                       \
    (((pa) & 0xFFF00000) | L1_TYPE_SECTION | L1_AP_FULL_ACCESS | \
     L1_MEM_DEVICE | L1_SHAREABLE | L1_XN | L1_DOMAIN_0)

/* Memory layout constants */
#define RAM_PA_BASE 0x80000000
#define RAM_VA_BASE 0xC0000000
#define RAM_SIZE_MB 256 /* Map 256MB of RAM */

#define MMIO_BASE 0x1C000000
#define MMIO_END 0x20000000 /* 64MB MMIO window */

/**
 * @brief Early boot page table initialization (runs before MMU)
 *
 * Sets up minimal L1 page table with:
 *   - Identity mapping for RAM at 0x80000000 (so current code keeps running)
 *   - Higher-half mapping for RAM at 0xC0000000 -> 0x80000000
 *   - Identity mapping for MMIO at 0x1C000000-0x20000000
 *
 * @param dtb_phys Physical address of DTB (unused for now, reserved for future)
 * @return Physical address of L1 table (for TTBR0)
 *
 * NOTE: This function is placed in .text.boot and runs at physical addresses
 */
__attribute__((section(".text.boot")))
uintptr_t
early_paging_init(uintptr_t dtb_phys)
{
    (void)dtb_phys; /* Reserved for future DTB-based memory detection */

    /* Zero the entire L1 table (4096 entries) */
    for (int i = 0; i < 4096; i++)
    {
        early_l1[i] = 0;
    }

    /*
     * Map RAM: Identity mapping (VA = PA)
     * VA 0x80000000 - 0x8FFFFFFF -> PA 0x80000000 - 0x8FFFFFFF
     *
     * L1 index = VA[31:20], so 0x800 = index 2048
     */
    for (unsigned int i = 0; i < RAM_SIZE_MB; i++)
    {
        uintptr_t pa = RAM_PA_BASE + (i << 20); /* 1MB sections */
        unsigned int idx = (RAM_PA_BASE >> 20) + i;
        early_l1[idx] = SECTION_NORMAL(pa);
    }

    /*
     * Map RAM: Higher-half mapping
     * VA 0xC0000000 - 0xCFFFFFFF -> PA 0x80000000 - 0x8FFFFFFF
     *
     * L1 index for 0xC0000000 = 0xC00 = 3072
     */
    for (unsigned int i = 0; i < RAM_SIZE_MB; i++)
    {
        uintptr_t pa = RAM_PA_BASE + (i << 20);
        unsigned int idx = (RAM_VA_BASE >> 20) + i;
        early_l1[idx] = SECTION_NORMAL(pa);
    }

    /*
     * Map MMIO: Identity mapping for vexpress peripherals
     * VA 0x1C000000 - 0x1FFFFFFF -> PA 0x1C000000 - 0x1FFFFFFF (device memory)
     *
     * L1 index for 0x1C000000 = 0x1C0 = 448
     */
    unsigned int mmio_sections = (MMIO_END - MMIO_BASE) >> 20; /* 64 sections */
    for (unsigned int i = 0; i < mmio_sections; i++)
    {
        uintptr_t pa = MMIO_BASE + (i << 20);
        unsigned int idx = (MMIO_BASE >> 20) + i;
        early_l1[idx] = SECTION_DEVICE(pa);
    }

    /*
     * Return physical address of L1 table for TTBR0
     * Since early_l1 is in .bss.boot which is at physical addresses,
     * the symbol value IS the physical address.
     */
    return (uintptr_t)early_l1;
}

// uint32_t bss_check;

_Noreturn void early(void *dtb_ptr)
{

    // Parse DTB and extract memory info
    dtb_init(dtb_ptr);
    // Set up hardcoded emergency console
#if defined(EARLY_UART)
    uart_set_driver(&pl011_driver, VEXPRESS_SMB_BASE + VEXPRESS_UART0_OFF);
    kprintf_init(uart_putc);
    KINFO("Early UART initialized");
#endif

    // Fill kernel_layout from linker symbols + DTB
    // NOTE: PMM works with PHYSICAL addresses, so use _kernel_phys_* symbols
    kernel_layout.dtb_start_pa = (uintptr_t)dtb_ptr;
    kernel_layout.kernel_start_pa = (uintptr_t)_kernel_phys_start;
    kernel_layout.kernel_end_pa = (uintptr_t)_kernel_phys_end;
    kernel_layout.stack_base_pa = (uintptr_t)__svc_stack_base__;
    kernel_layout.stack_top_pa = (uintptr_t)__svc_stack_top__;

    // Fill phys_region from DTB
    uint64_t ram_base, ram_size;
    if (!dtb_get_reg("/memory", 0, &ram_base, &ram_size))
    {
        panic("Failed to find memory cell in DTB");
    }
    phys_region.start = (uintptr_t)ram_base;
    phys_region.end = (uintptr_t)(ram_base + ram_size);

    //__asm__ volatile(".word 0xffffffff");

    // Initialize subsystems
    pmm_init();     
    kheap_init();    
    vmm_bootstrap(); 

    //__asm__ volatile(".word 0xffffffff");

    // Hand off to kernel proper
    kmain();
}

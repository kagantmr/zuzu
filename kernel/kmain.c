#include "arch/arm/include/symbols.h"
#include "arch/arm/include/irq.h"
#include "arch/arm/include/gicv2.h"
#include "arch/arm/include/timer.h"

#include "drivers/uart/uart.h"
#include "drivers/uart/pl011.h"

#include "core/version.h"

#include "core/kprintf.h"
#include "core/log.h"
#include "core/panic.h"
#include "core/assert.h"

#include "kernel/layout.h"
#include "kernel/mm/pmm.h"
#include "kernel/dtb/dtb.h"
#include "kernel/vmm/vmm.h"

#include "lib/mem.h"
#include "kernel/mm/alloc.h"

extern kernel_layout_t kernel_layout;
extern pmm_state_t pmm_state;
extern phys_region_t phys_region;

static inline uint32_t read_cpsr(void) {
    uint32_t cpsr;
    __asm__ volatile ("mrs %0, cpsr" : "=r"(cpsr));
    return cpsr;
}

static inline uint32_t cpu_mode_from_cpsr(uint32_t cpsr) {
    return cpsr & 0x1F;
}

static inline uint32_t read_be32(const void *p) {
    const uint8_t *b = (const uint8_t *)p;
    return ((uint32_t)b[0] << 24) |
           ((uint32_t)b[1] << 16) |
           ((uint32_t)b[2] << 8)  |
           ((uint32_t)b[3]);
}

static inline const char* cpu_mode_str(uint32_t mode) {
    switch (mode) {
        case 0x10: return "User";
        case 0x11: return "FIQ";
        case 0x12: return "IRQ";
        case 0x13: return "Supervisor";
        case 0x17: return "Abort";
        case 0x1B: return "Undefined";
        case 0x1F: return "System";
        default:   return "Unknown";
    }
}

void print_logo(void) {

    const char *art[] = {
        "                                               ",
        "                                               ",
        "                                               ",
        "      ███████████                              ",
        "            ████               ███      ███   ",
        "          ████                 ██████  ███    ",
        "         ███                  ███      ███    ",
        "       ████                   ███     ███     ",
        "     ████                    ███      ███     ",
        "    ███                      ███      ██      ",
        "   ████████████████████████  ███     ███      ",
        "                             ██████████       ",
        "          ███                   ████          ",
        "          ███                      ██         ",
        "          ███                     ███         ",
        "          ███    █████████████    ███         ",
        "          ███   ███         ███   ███         ",
        "          ███   ███         ███   ███         ",
        "          ███   ███         ███   ███         ",
        "          ████ ████         ████  ███         ",
        "           ██████            ██████           ",
        "                                               "
    };


        int n_lines = sizeof(art) / sizeof(art[0]);
    const char *cyan = "\033[1;36m"; // Bright cyan

    for (int i = 0; i < n_lines; i++) {
        kprintf("%s%s\033[0m\n", cyan, art[i]);
    }

    // Cyan color: \x1b[36m, Reset: \x1b[0m
    const char *logo[] = {
        " ________  ___  ___  ________  ___  ___     ",
        "|\\_____  \\|\\  \\|\\  \\|\\_____  \\|\\  \\|\\  \\    ",
        " \\|___/  /\\ \\  \\\\\\  \\\\|___/  /\\ \\  \\\\\\  \\   ",
        "     /  / /\\ \\  \\\\\\  \\   /  / /\\ \\  \\\\\\  \\  ",
        "    /  /_/__\\ \\  \\\\\\  \\ /  /_/__\\ \\  \\\\\\  \\ ",
        "   |\\________\\ \\_______\\\\________\\ \\_______\\",
        "    \\|_______|\\|_______|\\|_______|\\|_______|"
    };

    size_t lines = sizeof(logo) / sizeof(logo[0]);

    kprintf("\x1b[36m"); // Start cyan
    for (size_t i = 0; i < lines; i++) {
        kprintf("%s\n", logo[i]);
    }
    kprintf("\x1b[0m\n"); // Reset color
}

void print_boot_info(void)
{    
    KINFO("Zuzu kernel %s", ZUZU_VERSION);
    
    // Use dtb_get_string for string properties
    char machine_name[64];
    if (dtb_get_string("/", "model", machine_name, sizeof(machine_name))) {
        KINFO("Machine: %s", machine_name);
    } else {
        KINFO("Machine: Unknown");
    }
    
    char compatible[64];
    if (dtb_get_string("/", "compatible", compatible, sizeof(compatible))) {
        KINFO("Compatible: %s", compatible);
    }

    // RAM info
    kassert(pmm_state.pfn_end >= pmm_state.pfn_base);
    kassert(pmm_state.total_pages == (size_t)(pmm_state.pfn_end - pmm_state.pfn_base));
    uint32_t ram_size_mb = pmm_state.total_pages * PAGE_SIZE / 1024 / 1024;

    if (ram_size_mb < 64) {
        kprintf("\n");
        KWARN("%u MB of RAM detected. Zuzu recommends 64MB+ for smooth functioning.\n", ram_size_mb);
    } else {
        KINFO("%u MB of RAM detected", ram_size_mb);
    }

    // CPU info
    uint32_t cpsr = read_cpsr();
    uint32_t mode = cpu_mode_from_cpsr(cpsr);
    KINFO("CPU mode: %s (CPSR=0x%x)", cpu_mode_str(mode), cpsr);

    uint32_t sp;
    __asm__ volatile("mov %0, sp" : "=r"(sp));
    KINFO("SP: %p", (void *)sp);

    // Memory layout
    KINFO("Kernel start: %p", (void *)_kernel_start);
    KINFO("Kernel end:   %p", (void *)_kernel_end);

    kassert(pmm_state.bitmap != NULL);
    KINFO("Bitmap start: %p", (void *)pmm_state.bitmap);
    KINFO("Bitmap end:   %p", (void *)(pmm_state.bitmap + pmm_state.bitmap_bytes));

    KINFO("Free pages: %u, Total pages: %u", pmm_state.free_pages, pmm_state.total_pages);

    KINFO("Stack base (VA): %p", (void *)kernel_layout.stack_base_va);
    KINFO("Stack top  (VA): %p", (void *)kernel_layout.stack_top_va);

    KINFO("Heap start (VA): %p", kernel_layout.heap_start_va);
    KINFO("Heap end   (VA): %p", kernel_layout.heap_end_va);
}


_Noreturn void kmain(void) {

    // === CRITICAL: Complete VMM transition first ===
    // This removes identity mapping and relocates ALL mode stacks to higher-half.
    // Must happen before ANY other code runs in kmain.
    // 
    // NOTE: Interrupts should already be disabled from early boot,
    // but we disable them explicitly here for safety since we're
    // modifying IRQ/ABT/UND mode stack pointers.
    __asm__ volatile("cpsid if");  // Disable IRQ and FIQ
    
    // Establish VA companions for boot-time physical addresses while both mappings exist.
    // After identity removal, only *_va fields should be dereferenced.
    kernel_layout.dtb_start_va = (void *)PA_TO_VA(kernel_layout.dtb_start_pa);
    kernel_layout.stack_base_va = (uintptr_t)PA_TO_VA(kernel_layout.stack_base_pa);
    kernel_layout.stack_top_va  = (uintptr_t)PA_TO_VA(kernel_layout.stack_top_pa);
    kernel_layout.kernel_start_va = (uintptr_t)_kernel_start;
    kernel_layout.kernel_end_va   = (uintptr_t)_kernel_end;


    // Copy DTB into heap so it remains valid after identity mapping is removed.
    // DTB header fields are big-endian.
    void *boot_dtb = kernel_layout.dtb_start_va;

    uint32_t magic = read_be32((uint8_t *)boot_dtb + 0x00);
    kassert(magic == 0xD00DFEED);

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
    if (kernel_layout.heap_start_va == NULL) {
        if (kernel_layout.heap_start_pa >= KERNEL_VA_BASE) {
            kernel_layout.heap_start_va = (void *)kernel_layout.heap_start_pa;
        } else {
            kernel_layout.heap_start_va = (void *)PA_TO_VA(kernel_layout.heap_start_pa);
        }
    }
    if (kernel_layout.heap_end_va == NULL) {
        if (kernel_layout.heap_end_pa >= KERNEL_VA_BASE) {
            kernel_layout.heap_end_va = (void *)kernel_layout.heap_end_pa;
        } else {
            kernel_layout.heap_end_va = (void *)PA_TO_VA(kernel_layout.heap_end_pa);
        }
    }


    // From this point onward, use dtb_start_va
    dtb_init(kernel_layout.dtb_start_va);


    #ifndef EARLY_UART
    char uart_path[128];
    uint64_t uart_base, uart_size;

    // Find first PL011

    if (dtb_find_compatible("arm,pl011", uart_path, sizeof(uart_path)))
    {
        if (dtb_get_reg_phys(uart_path, 0, &uart_base, &uart_size))
        {
            uart_set_driver(&pl011_driver, (uintptr_t)uart_base);
            kprintf_init(uart_putc);
            KINFO("UART initialized: %s @ 0x%x", uart_path, uart_base);
        }
    }
    #endif

    // Interrupts stay disabled until GIC is initialized

    uint64_t gicd_addr, gicd_size, gicc_addr, gicc_size;
    char gic_path[128];

    if (dtb_find_compatible("arm,cortex-a15-gic", gic_path, sizeof(gic_path))) {
        // reg has 4 entries: GICD, GICC, GICH, GICV
        dtb_get_reg(gic_path, 0, &gicd_addr, &gicd_size);  // GICD
        dtb_get_reg(gic_path, 1, &gicc_addr, &gicc_size);  // GICC
        
        gic_init((uintptr_t)gicd_addr, (uintptr_t)gicc_addr);  // Pass addresses to init
    } else {
        KPANIC("GIC not found in DTB");
    }

    irq_init();
    timer_init();

    arch_global_irq_enable();  // Only after GIC is initialized

    KINFO("Booting...");

    // data abort test (null deref)
    // enable only when explicitly debugging the abort path
    //__asm__ volatile("mov r0, #0\n\tldr r0, [r0]"); 
    
    print_logo();
    print_boot_info();

    
    // trigger SVC to verify exception handling
    //__asm__ volatile("svc #0");

    KINFO("Entering idle");
    while (1) {
        __asm__("wfi");
    }
}
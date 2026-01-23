#include "arch/arm/include/symbols.h"
#include "arch/arm/include/irq.h"

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

#define ZUZU_VER "v0.0.1"

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

    KINFO("Stack base: %p", (void *)kernel_layout.stack_base);
    KINFO("Stack top:  %p", (void *)kernel_layout.stack_top);
}


_Noreturn void kmain(void) {

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
            KINFO("UART initialized: %s @ 0x%lx", uart_path, uart_base);
        }
    }
    #endif

    KINFO("Booting...");
    
    print_logo();
    print_boot_info();   // All the KINFO diagnostic spam
    
    // TODO: GIC init here
    // TODO: Timer init here
    arch_global_irq_enable();  // Only after GIC!
    
    //__asm__ volatile("svc #0");

    KINFO("Entering idle");
    while (1) {
        __asm__("wfi");
    }
}


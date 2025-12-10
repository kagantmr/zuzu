
#include "symbols.h"
#include "uart.h"
#include "version.h"
#include "kprintf.h"
#include "log.h"
#include "panic.h"
#include "phys.h"

#include "layout.h"

#include "pmm.h"

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

void kmain(void)
{
    kprintf_init(uart_putc);
    KINFO("early() complete");
    KINFO("UART driver initialized");

    KINFO("Booting...");

    print_logo();


    KINFO("Zuzu kernel %s", ZUZU_VERSION);
    KINFO("Machine: %s", "QEMU Vexpress-A15");
    KINFO("RAM: %u MB", (unsigned)(phys_region.end - phys_region.start / (1024 * 1024)));

    uint32_t cpsr = read_cpsr();
    uint32_t mode = cpu_mode_from_cpsr(cpsr);
    KINFO("CPU mode: %s (CPSR=0x%x)", cpu_mode_str(mode), cpsr);

    uint32_t sp;
    __asm__ volatile("mov %0, sp" : "=r"(sp));
    KINFO("SP: %p", (void *)sp);

    KINFO("Kernel start: %p", (void *)_kernel_start);
    KINFO("Kernel end:   %p", (void *)_kernel_end);

    KINFO("Bitmap start: %p", (void *)pmm_state.bitmap);
    KINFO("Bitmap end:   %p", (void *)pmm_state.bitmap + pmm_state.bitmap_bytes);

    KINFO("free pages: %u, total pages: %u", pmm_state.free_pages, pmm_state.total_pages);

    KINFO("Stack base:    %p", (void *)kernel_layout.stack_base);
    KINFO("Stack top:     %p", (void *)kernel_layout.stack_top);

    KINFO("Kernel init complete. Entering idle.");
    while (1)
    {
        __asm__("wfi");
    }
}

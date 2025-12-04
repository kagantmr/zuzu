#include "arch/arm/platform.h"
#include "arch/arm/symbols.h"
#include "arch/arm/cpu.h"

#include "drivers/uart.h"

#include "core/version.h"

#include "core/kprintf.h"
#include "core/log.h"
#include "core/panic.h"

#include "kernel/include/layout.h"

#include "kernel/mm/pmm.h"

#define ZUZU_VER "v0.0.1"

extern kernel_layout_t kernel_layout;
extern pmm_state_t pmm_state;

void kmain(void) {
    

    uart_init();
    kprintf_init(uart_putc);
    KINFO("early() complete");
    KINFO("UART driver initialized");
    

    KINFO("Booting...\n");

    kprintf("\033[36m ________  ___  ___  ________  ___  ___     \033[0m\n");
    kprintf("\033[36m|\\_____  \\|\\  \\|\\  \\|\\_____  \\|\\  \\|\\  \\    \033[0m\n");
    kprintf("\033[36m \\|___/  /\\ \\  \\\\\\  \\\\|___/  /\\ \\  \\\\\\  \\   \033[0m\n");
    kprintf("\033[36m     /  / /\\ \\  \\\\\\  \\   /  / /\\ \\  \\\\\\  \\  \033[0m\n");
    kprintf("\033[36m    /  /_/__\\ \\  \\\\\\  \\ /  /_/__\\ \\  \\\\\\  \\ \033[0m\n");
    kprintf("\033[36m   |\\________\\ \\_______\\\\________\\ \\_______\\\033[0m\n");
    kprintf("\033[36m    \\|_______|\\|_______|\\|_______|\\|_______|\033[0m\n");
    kprintf("\n");


    KINFO("Zuzu kernel %s", ZUZU_VERSION);
    KINFO("Machine: %s", ZUZU_MACHINE);
    KINFO("RAM: %u MB", (unsigned)(ZUZU_RAM_SIZE_BYTES / (1024 * 1024)));

    uint32_t cpsr = read_cpsr();
    uint32_t mode = cpu_mode_from_cpsr(cpsr);
    KINFO("CPU mode: %s (CPSR=0x%x)", cpu_mode_str(mode), cpsr);

    uint32_t sp;
    __asm__ volatile ("mov %0, sp" : "=r"(sp));
    KINFO("SP: %p", (void*)sp);

    KINFO("Kernel start: %p", (void*)_kernel_start);
    KINFO("Kernel end:   %p", (void*)_kernel_end);

    KINFO("Bitmap start: %p", (void*)pmm_state.bitmap);
    KINFO("Bitmap end:   %p", (void*)pmm_state.bitmap + pmm_state.bitmap_bytes);

    KINFO("free pages: %d, total pages: %d", pmm_state.free_pages, pmm_state.total_pages);

    KINFO("Stack base:    %p", (void*)kernel_layout.stack_base);
    KINFO("Stack top:     %p", (void*)kernel_layout.stack_top);
    


    KINFO("Kernel init complete. Entering idle.");
    while (1) {
        __asm__("wfi");
    }
}

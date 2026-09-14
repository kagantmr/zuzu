/* Boot constants for the shared path in arch/arm/boot/_start.S.
 * BCM2711 in low-peripheral mode: RAM at 0, peripherals 0xFE000000-0xFFFFFFFF,
 * PL011 uart0 at 0xFE201000, firmware default UARTCLK 48 MHz (pinned by
 * init_uart_clock in scripts/rpi4/config.txt). */
#define BOARD_KERNEL_VA_OFFSET  0xC0000000
#define BOARD_RAM_PA_BASE       0x00000000
#define BOARD_PERIPH_PA_BASE    0xFE000000
#define BOARD_PERIPH_MB         32
#define BOARD_UART0_BASE        0xFE201000
#define BOARD_UART_IBRD         26      /* 48e6 / (16 * 115200) = 26.042 */
#define BOARD_UART_FBRD         3       /* round(0.042 * 64) */

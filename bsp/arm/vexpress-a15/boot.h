/* Boot constants for the shared path in arch/arm/boot/_start.S.
 * Versatile Express A15: RAM at 0x80000000, peripherals on smb chip-select 3
 * (iofpga) at 0x1C000000, uart0 at +0x90000, clk24mhz. */
#define BOARD_KERNEL_VA_OFFSET  0x40000000
#define BOARD_RAM_PA_BASE       0x80000000
#define BOARD_PERIPH_PA_BASE    0x1C000000
#define BOARD_PERIPH_MB         2
#define BOARD_UART0_BASE        0x1C090000
#define BOARD_UART_IBRD         13      /* 24e6 / (16 * 115200) = 13.02 */
#define BOARD_UART_FBRD         1       /* round(0.02 * 64) */

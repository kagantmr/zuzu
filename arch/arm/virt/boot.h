/* Boot constants for the shared path in arch/arm/boot/_start.S.
 * QEMU virt: RAM at 0x40000000, PL011 + PL031 in one megabyte at 0x09000000,
 * apb-pclk 24 MHz. */
#define BOARD_KERNEL_VA_OFFSET  0x80000000
#define BOARD_RAM_PA_BASE       0x40000000
#define BOARD_PERIPH_PA_BASE    0x09000000
#define BOARD_PERIPH_MB         1
#define BOARD_UART0_BASE        0x09000000
#define BOARD_UART_IBRD         13      /* 24e6 / (16 * 115200) = 13.02 */
#define BOARD_UART_FBRD         1       /* round(0.02 * 64) */

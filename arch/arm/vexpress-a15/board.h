#ifndef VEXPRESS_BOARD_H
#define VEXPRESS_BOARD_H

// The Versatile Express Motherboard puts peripherals on the CS3 (IOFPGA) bus.
// This bus is mapped to physical address 0x1C000000.
#define VEXPRESS_SMB_BASE   0x1C000000

// Offsets for specific devices (relative to SMB_BASE)
#define VEXPRESS_UART0_OFF  0x090000

#define VEXPRESS_UART0 1

void board_init_devices(void);

#endif
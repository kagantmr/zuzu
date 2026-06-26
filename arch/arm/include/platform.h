/**
 * board.h - Architecture-level board interface (board-independent)
 *
 * The early boot path and device bring-up are shared across all ARM boards;
 * board-specific differences are expressed through the DTB and the per-board
 * layout.h / linker.ld / _start.S, not through C code. A new board therefore
 * does not reimplement board_init_devices() — it only supplies its memory map
 * and boot stub.
 */

#ifndef ARCH_ARM_BOARD_H
#define ARCH_ARM_BOARD_H

/* Discover and initialize platform devices (UART, GIC, timer, ...) from the
 * DTB. Shared implementation lives in arch/arm/board.c. */
void board_init_devices(void);

#endif /* ARCH_ARM_BOARD_H */

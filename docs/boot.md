# zuzu Boot Process

This document outlines the boot process of the Zuzu kernel on the VExpress-A15 platform, detailing the steps taken from power-on to the kmainc() function.

## 1. Reset and Initial Assembly

Upon reset, the CPU starts executing from a predefined address. `_start.s` runs first, and performs the following tasks:
1. Initializes the stack pointer for all CPU modes.
2. Enables the MMU with an identity mapping.
3. Jumps to post-MMU assembly code.
4. Sets up the exception vector table.
5. Clears the BSS segment to zero.
6. Saves the DTB (Device Tree Blob) pointer for later use.
7. After the C environment is ready, branches to `early.c`.

## 2. Early Console Initialization

If zuzu was compiled with the EARLY_UART flag set, UART will be initialized (board-specific).

## 3. C Environment Setup (early.c)
## 4. Kernel Main Function (kmain.c)

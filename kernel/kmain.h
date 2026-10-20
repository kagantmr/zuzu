/**
 * @file kmain.h
 * @brief Kernel main entry point.
 */

#ifndef KERNEL_KMAIN_H
#define KERNEL_KMAIN_H

/**
 * @brief Kernel entry point once early boot is done: start the scheduler, load the root service and never return.
 *
 * Panics if the bootloader supplied no initrd.
 */
void kmain(void) __attribute__((noreturn));

#endif

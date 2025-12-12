#ifndef PANIC_H
#define PANIC_H

/**
 * @brief Handle a kernel panic by disabling interrupts and halting.
 * This function does not return.
 */
void panic();

#endif
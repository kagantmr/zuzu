#ifndef PANIC_H
#define PANIC_H

#include <stdint.h>


/**
 * @brief Handle a kernel panic by disabling interrupts and halting.
 * This function does not return.
 */
_Noreturn void panic(void);

/**
 * @brief Dump the current stack trace.
 * @param fp Optional frame pointer to start from (NULL = use current FP)
 */
void dump_stack();



#endif
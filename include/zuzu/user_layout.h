/**
 * @file user_layout.h
 * @brief User virtual memory layout.
 */

#ifndef ZUZU_USER_LAYOUT_H
#define ZUZU_USER_LAYOUT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <arch_impl/user_layout.h>

/**
 * @brief How many stack pages a task comes with.
 */
#define USER_STACK_PAGES 4u

/**
 * @brief Size of the stack, calculated from USER_STACK_PAGES.
 */
#define USER_STACK_SIZE (USER_STACK_PAGES * 0x1000UL)

#ifdef __cplusplus
}
#endif

#endif

#ifndef ZUZU_USER_LAYOUT_H
#define ZUZU_USER_LAYOUT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include BOARD_LAYOUT_H

#define USER_STACK_PAGES     4u
#define USER_STACK_SIZE      (USER_STACK_PAGES * 0x1000UL)

#ifdef __cplusplus
}
#endif

#endif
#ifndef LAYOUT_H
#define LAYOUT_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    uintptr_t kernel_start;   // from linker symbol
    uintptr_t kernel_end;     // from linker symbol

    uintptr_t stack_base;     // computed in early boot C
    uintptr_t stack_top;      // computed in early boot C

    uintptr_t bitmap_start;   // assigned during PMM init
    uintptr_t bitmap_end;

    uintptr_t heap_start;     // after bitmap / boot alloc
    uintptr_t heap_end;       // later, when you know RAM size
} kernel_layout_t;



#endif
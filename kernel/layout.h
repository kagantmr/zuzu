#ifndef LAYOUT_H
#define LAYOUT_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    uintptr_t start;   // physical start address of RAM region
    uintptr_t end;     // physical end address (exclusive)
} phys_region_t;



typedef struct {
    uintptr_t dtb_start_pa;      // provided by bootloader
    void     *dtb_start_va;      // mapped VA used by DTB parser after identity removal

    uintptr_t kernel_start_pa;   // from linker symbol
    uintptr_t kernel_end_pa;     // from linker symbol
    uintptr_t kernel_start_va;   // higher-half VA (from linker)
    uintptr_t kernel_end_va;     // higher-half VA (from linker)

    uintptr_t stack_base_pa;     // computed in early boot C
    uintptr_t stack_top_pa;      // computed in early boot C
    uintptr_t stack_base_va;     // active kernel VA stack region
    uintptr_t stack_top_va;

    uintptr_t bitmap_start_pa;   // assigned during PMM init
    uintptr_t bitmap_end_pa;
    uint8_t  *bitmap_va;         // dereferenceable VA pointer to bitmap

    uintptr_t heap_start_pa;     // after bitmap / boot alloc
    uintptr_t heap_end_pa;       // later, when you know RAM size
    void     *heap_start_va;     // VA used by kmalloc/heap
    void     *heap_end_va;       // optional

} kernel_layout_t;


#endif
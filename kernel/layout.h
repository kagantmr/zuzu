#ifndef KERNEL_LAYOUT_H
#define KERNEL_LAYOUT_H

#include <stddef.h>
#include <stdint.h>

typedef struct {

    uintptr_t ram_start;
    uintptr_t ram_end;

    uintptr_t dtb_start_pa;

    uintptr_t kernel_start_pa;
    uintptr_t kernel_end_pa;
    uintptr_t kernel_start_va;
    uintptr_t kernel_end_va;

    uintptr_t stack_base_pa;
    uintptr_t stack_top_pa;
    uintptr_t stack_base_va;
    uintptr_t stack_top_va;

    uintptr_t bitmap_start_pa;
    uintptr_t bitmap_end_pa;
    uint8_t *bitmap_va;

    uintptr_t heap_start_pa;
    uintptr_t heap_end_pa;
    void *heap_start_va;
    void *heap_end_va;

} RamLayout;

#endif

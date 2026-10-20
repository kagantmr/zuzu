/**
 * @file layout.h
 * @brief Description of the kernel's physical and virtual memory layout.
 */

#ifndef KERNEL_LAYOUT_H
#define KERNEL_LAYOUT_H

#include <stddef.h>
#include <stdint.h>

/**
 * @brief Where the kernel, its stack, the frame bitmap and the heap sit in RAM.
 */
typedef struct {
    uintptr_t ram_start; /**< Physical start of RAM. */
    uintptr_t ram_end; /**< Physical end of RAM. */

    uintptr_t dtb_start_pa; /**< Physical address of the device tree blob. */

    uintptr_t kernel_start_pa; /**< Physical start of the kernel image. */
    uintptr_t kernel_end_pa; /**< Physical end of the kernel image. */
    uintptr_t kernel_start_va; /**< Virtual start of the kernel image. */
    uintptr_t kernel_end_va; /**< Virtual end of the kernel image. */

    uintptr_t stack_base_pa; /**< Physical low end of the boot stack. */
    uintptr_t stack_top_pa; /**< Physical high end of the boot stack. */
    uintptr_t stack_base_va; /**< Virtual low end of the boot stack. */
    uintptr_t stack_top_va; /**< Virtual high end of the boot stack. */

    uintptr_t bitmap_start_pa; /**< Physical start of the frame allocator bitmap. */
    uintptr_t bitmap_end_pa; /**< Physical end of the frame allocator bitmap. */
    uint8_t *bitmap_va; /**< Virtual address of the frame allocator bitmap. */

    uintptr_t heap_start_pa; /**< Physical start of the kernel heap. */
    uintptr_t heap_end_pa; /**< Physical end of the kernel heap. */
    void *heap_start_va; /**< Virtual start of the kernel heap. */
    void *heap_end_va; /**< Virtual end of the kernel heap. */
} RamLayout;

#endif

/**
 * @file layout.h
 * @brief Description of the kernel's physical and virtual memory layout.
 */

#ifndef KERNEL_LAYOUT_H
#define KERNEL_LAYOUT_H

#include <stddef.h>
#include <stdint.h>
#include <types.h>

/** @brief Most RAM banks the kernel records. */
#define RAM_BANKS_MAX 4

/**
 * @brief One physically contiguous range of RAM.
 */
typedef struct {
    PhysAddr start; /**< Physical start. */
    PhysAddr end;   /**< Physical end, exclusive. */
} RamBank;

/**
 * @brief Where the kernel, its stack, the frame bitmap and the heap sit in RAM.
 */
typedef struct {
    PhysAddr ram_start; /**< Physical start of the lowest RAM bank. */
    PhysAddr ram_end; /**< Physical end of the highest RAM bank; holes between banks lie inside. */

    PhysAddr dtb_start_pa; /**< Physical address of the device tree blob. */

    PhysAddr boot_start_pa; /**< Physical start of the boot-only section. */
    PhysAddr boot_end_pa; /**< Physical end of the boot-only section. */

    PhysAddr kernel_start_pa; /**< Physical start of the kernel image. */
    PhysAddr kernel_end_pa; /**< Physical end of the kernel image. */
    VirtAddr kernel_start_va; /**< Virtual start of the kernel image. */
    VirtAddr kernel_end_va; /**< Virtual end of the kernel image. */

    PhysAddr stack_base_pa; /**< Physical low end of the boot stack. */
    PhysAddr stack_top_pa; /**< Physical high end of the boot stack. */
    VirtAddr stack_base_va; /**< Virtual low end of the boot stack. */
    VirtAddr stack_top_va; /**< Virtual high end of the boot stack. */

    PhysAddr bitmap_start_pa; /**< Physical start of the frame allocator bitmap. */
    PhysAddr bitmap_end_pa; /**< Physical end of the frame allocator bitmap. */
    uint8_t *bitmap_va; /**< Virtual address of the frame allocator bitmap. */

    PhysAddr heap_start_pa; /**< Physical start of the kernel heap. */
    PhysAddr heap_end_pa; /**< Physical end of the kernel heap. */
    void *heap_start_va; /**< Virtual start of the kernel heap. */
    void *heap_end_va; /**< Virtual end of the kernel heap. */

    RamBank ram_banks[RAM_BANKS_MAX]; /**< RAM banks from the device tree, sorted by address. */
    size_t ram_bank_count; /**< Number of entries in ram_banks. */
} RamLayout;

#endif

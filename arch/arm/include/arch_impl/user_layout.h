/**
 * @file
 * @brief ARMv7-A user address-space layout.
 */

#ifndef ARCH_ARM_USER_LAYOUT_H
#define ARCH_ARM_USER_LAYOUT_H

/** @brief End of user space; TTBR0 maps everything below (TTBCR.N = 1). */
#define USER_VA_TOP 0x80000000UL

/** @brief Where the syspage is mapped in every space. */
#define USER_SYSPAGE_VA 0x00001000UL
/** @brief Where the boot information pages are mapped in the root service. */
#define USER_BOOTINFO_VA 0x00002000UL
/** @brief Lowest address program segments are loaded at. */
#define USER_ELF_BASE 0x00010000UL
/** @brief Start of the area for mappings without a fixed address. */
#define USER_MMAP_BASE 0x20000000UL
/** @brief Start of the device-mapping window; end of the general mapping area. */
#define USER_DEVICE_BASE 0x7F000000UL
/** @brief End of the device-mapping window. */
#define USER_DEVICE_LIMIT 0x7F7FE000UL
/** @brief Unmapped guard page below the stack reserve. */
#define USER_STACK_GUARD_VA 0x7F7FF000UL
/** @brief Bottom of the 8 MiB stack reserve, faulted in on demand. */
#define USER_STACK_BASE 0x7F800000UL
/** @brief Top of the stack reserve. */
#define USER_STACK_TOP 0x80000000UL
/** @brief Initial stack pointer of a new program; argv is laid out just below it. */
#define USR_SP 0x7FFFE000UL

#endif

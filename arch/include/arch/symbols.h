/**
 * @file
 * @brief Addresses defined by the kernel linker script.
 */

#ifndef ARCH_SYMBOLS_H
#define ARCH_SYMBOLS_H

extern char _kernel_start[]; /**< Virtual start of the kernel image. */
extern char _kernel_end[];   /**< Virtual end of the kernel image. */
extern char _rodata_start[]; /**< Virtual start of .rodata, page-aligned; .text ends here. */
extern char _data_start[];   /**< Virtual start of .data, page-aligned; .rodata ends here. */

extern char _kernel_phys_start[];    /**< Physical start of the kernel image. */
extern char _kernel_phys_end[];      /**< Physical end of the kernel image. */
extern char _boot_start[];           /**< Start of the boot-only section, freed after early boot. */
extern char _boot_end[];             /**< End of the boot-only section. */
extern char __kstack_base[];         /**< Virtual kernel stack base. */
extern char __kstack_top[];          /**< Virtual kernel stack top. */
extern char _boot_start_va[];        /**< Start of the boot region. */
extern char _boot_end_va[];          /**< End of the boot region. */

#endif // ARCH_SYMBOLS_H

/**
 * @file
 * @brief Addresses defined by the kernel linker script.
 */

#ifndef ARCH_SYMBOLS_H
#define ARCH_SYMBOLS_H

extern char _kernel_start[]; /**< Virtual start of the kernel image. */
extern char _kernel_end[];   /**< Virtual end of the kernel image. */

extern char _kernel_phys_start[];    /**< Physical start of the kernel image. */
extern char _kernel_phys_end[];      /**< Physical end of the kernel image. */
extern char _boot_start[];           /**< Start of the boot-only section, freed after early boot. */
extern char _boot_end[];             /**< End of the boot-only section. */
extern char __stack_region_base__[]; /**< Lowest address of the exception-mode stacks. */
extern char __stack_region_end__[];  /**< End of the exception-mode stacks. */
extern char __svc_stack_base__[];    /**< Bottom of the boot SVC-mode stack. */
extern char __svc_stack_top__[];     /**< Top of the boot SVC-mode stack. */
extern char __irq_stack_base__[];    /**< Bottom of the IRQ-mode stack. */
extern char __irq_stack_top__[];     /**< Top of the IRQ-mode stack. */
extern char __abt_stack_base__[];    /**< Bottom of the abort-mode stack. */
extern char __abt_stack_top__[];     /**< Top of the abort-mode stack. */
extern char __und_stack_base__[];    /**< Bottom of the undefined-mode stack. */
extern char __und_stack_top__[];     /**< Top of the undefined-mode stack. */

#endif // ARCH_SYMBOLS_H

#ifndef SYMBOLS_H
#define SYMBOLS_H

#include <stdint.h>

/* Virtual addresses (for code references) */
extern char _kernel_start[];
extern char _kernel_end[];

/* Physical addresses (for PMM/memory management) */
extern char _kernel_phys_start[];
extern char _kernel_phys_end[];
extern char _boot_start[];
extern char _boot_end[];
extern char __svc_stack_base__[];
extern char __svc_stack_top__[];
extern char __irq_stack_base__[];
extern char __irq_stack_top__[];
extern char __abt_stack_base__[];
extern char __abt_stack_top__[];
extern char __und_stack_base__[];
extern char __und_stack_top__[];


#endif
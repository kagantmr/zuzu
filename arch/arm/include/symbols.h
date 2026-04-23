// symbols.h - ARM linker symbol declarations
// This file declares the external symbols that are defined in the linker script (linker.ld) and used in the kernel code. 
// These symbols represent the start and end addresses of various sections of the kernel in both virtual and physical memory, as well as the stack regions for different CPU modes.

#ifndef SYMBOLS_H
#define SYMBOLS_H

extern char _kernel_start[];
extern char _kernel_end[];

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
extern char _initrd_start[];
extern char _initrd_end[];


#endif
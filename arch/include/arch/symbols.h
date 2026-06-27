// arch/symbols.h - Neutral linker-symbol contract.
//
// These symbols are emitted by the per-board linker script and mark kernel
// image / stack / initrd boundaries. Names are architecture-neutral; every
// board's linker.ld must define them.

#ifndef ZUZU_ARCH_SYMBOLS_H
#define ZUZU_ARCH_SYMBOLS_H

extern char _kernel_start[];
extern char _kernel_end[];

extern char _kernel_phys_start[];
extern char _kernel_phys_end[];
extern char _boot_start[];
extern char _boot_end[];
extern char __stack_region_base__[];
extern char __stack_region_end__[];
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

#endif // ZUZU_ARCH_SYMBOLS_H

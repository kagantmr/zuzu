#ifndef SYMBOLS_H
#define SYMBOLS_H

#include <stdint.h>

extern char _kernel_start[];
extern char _kernel_end[];
extern char __svc_stack_base__[];
extern char __svc_stack_top__[];
extern char __irq_stack_base__[];
extern char __irq_stack_top__[];
extern char __abt_stack_base__[];
extern char __abt_stack_top__[];
extern char __und_stack_base__[];
extern char __und_stack_top__[];


#endif
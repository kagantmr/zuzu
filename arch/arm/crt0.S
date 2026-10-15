.section .text, "ax"
.arm

#include <arch_impl/svc.h>
.global _start

_start:
    mov r6, r0
    mov r7, r1

#ifdef ZUZU_NEWLIB

    bl __libc_init_array
    mov r0, r6
    mov r1, r7
    bl main
    bl exit
3:  b 3b

.global _init
.type _init, %function
_init:
    bx lr
.size _init, . - _init

.global _fini
.type _fini, %function
_fini:
    bx lr
.size _fini, . - _fini
#else
    ldr r4, =__init_array_start
    ldr r5, =__init_array_end
1:
    cmp r4, r5
    bge 2f
    ldr r3, [r4], #4
    blx r3
    b 1b
2:
    mov r0, r6
    mov r1, r7
    bl main

    mov r6, r0
    ldr r4, =__fini_array_start
    ldr r5, =__fini_array_end
4:
    cmp r4, r5
    bge 5f
    sub r5, r5, #4
    ldr r3, [r5]
    blx r3
    b 4b
5:
    mov r0, r6
    mov r12, #0
    svc #ARCH_SVC_MAGIC
3:  b 3b
#endif

.size _start, . - _start

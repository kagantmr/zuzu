// crt0.s - userspace C runtime startup code for ARMv7-A

.section .text, "ax"
// The kernel kickstarts _start in ARM state, so crt0 must assemble as ARM.
.arm
.global _start

_start:
    mov r6, r0              // save argc
    mov r7, r1              // save argv

#ifdef ZUZU_NEWLIB

    bl __libc_init_array
    mov r0, r6              // argc
    mov r1, r7              // argv
    bl main
    bl exit                 // r0 = main's return value; does not return
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
    // run constructors
    ldr r4, =__init_array_start
    ldr r5, =__init_array_end
1:
    cmp r4, r5
    bge 2f
    ldr r3, [r4], #4
    blx r3
    b 1b
2:
    mov r0, r6              // argc
    mov r1, r7              // argv
    bl main

    // run destructors in reverse order
    mov r6, r0              // save main's return value
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
    svc #0
3:  b 3b
#endif

.size _start, . - _start
.section .text, "ax"
.global _start

_start:
    @ run constructors
    ldr r4, =__init_array_start
    ldr r5, =__init_array_end
1:
    cmp r4, r5
    bge 2f
    ldr r3, [r4], #4
    blx r3
    b 1b
2:
    bl main
    mov r0, r0
    svc #0
3:  b 3b

.size _start, . - _start
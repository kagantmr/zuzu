.global _start
_start:
    ldr sp, =__stack_base__   @ Initialize stack pointer
    cpsid i                   @ Disable interrupts
    cpsid f                   @ Disable FIQs
    bl clear_bss              @ Clear BSS section
    mov r0, r2                @ Pass DTB address in r0
    bl early                  @ Jump to C code
1:  b 1b                      @ Loop forever
.size _start, . - _start

clear_bss:
    ldr r0, =_bss_start
    ldr r1, =_bss_end
    mov r3, #0
1:
    cmp r0, r1
    bge 2f
    str r3, [r0], #4
    b 1b
2:
    bx lr
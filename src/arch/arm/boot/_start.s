.global _start
_start:
    ldr sp, =__stack_base__    @ Initialize stack pointer
    bl clear_bss              @ Clear BSS section  
    bl kernel_early           @ Jump to C code
1:  b 1b                      @ Loop forever
.size _start, . - _start

clear_bss:
    ldr r0, =_bss_start
    ldr r1, =_bss_end
    mov r2, #0
1:
    cmp r0, r1
    bge 2f
    str r2, [r0], #4
    b 1b
2:
    bx lr
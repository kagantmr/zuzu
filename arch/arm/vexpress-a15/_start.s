.global _start
_start:
    bl clear_mode_sp
    cpsid i                   @ Disable interrupts
    cpsid f                   @ Disable FIQs
    bl clear_bss              @ Clear BSS section


    ldr r2, =vector_table     @ Pass exception vector table
    mcr p15, 0, r2, c12, c0, 0   @ VBAR = vector_table
    isb

    ldr r2, =__dtb_addr__      @ Load DTB address
    mov r0, r2                @ Pass DTB address in r0

    bl early                  @ Jump to C code
1:  b 1b                      @ Loop forever
.size _start, . - _start

clear_mode_sp:
    cpsid i
    cpsid f
    @ cps #0x10            @ User mode (ignored currently)
    cps #0x12                 @ IRQ mode
    ldr sp, =__irq_stack_top__   @ Initialize stack pointer for IRQ
    cps #0x17                 @ Abort mode
    ldr sp, =__abt_stack_top__   @ Initialize stack pointer for ABT
    cps #0x1b                 @ Undefined mode
    ldr sp, =__und_stack_top__   @ Initialize stack pointer for UND
    cps #0x13                 @ Supervisor mode
    ldr sp, =__svc_stack_top__   @ Initialize stack pointer for SVC
    bx lr

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
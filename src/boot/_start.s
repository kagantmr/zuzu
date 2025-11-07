.global _start
_start:
    ldr sp, =STACK_TOP @ Initialize stack pointer
    bl start           @ Jump to C code
1:  b 1b               @ Loop forever
.size _start, . - _start
.global _start
_start:
    ldr sp, =boot_stack_top @ Initialize stack pointer
    bl kernel_early           @ Jump to C code
1:  b 1b               @ Loop forever
.size _start, . - _start

.bss
    .align 3          @ 8-byte alignment for ARM EABI
boot_stack:
    .space 1024       @ 1 KB
boot_stack_top:
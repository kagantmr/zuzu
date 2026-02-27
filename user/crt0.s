.section .text, "ax"
.global _start

_start:
    bl main
    mov r0, r0 @ exit code
    svc #0     @ SYS_QUIT
1:  b 1b
.size _start, . - _start
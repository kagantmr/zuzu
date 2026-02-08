.syntax unified
.arm

.global reset_handler
.global undef_handler
.global svc_handler
.global prefetch_abort_handler
.global data_abort_handler
.global reserved_handler
.global irq_handler
.global fiq_handler
.global process_entry_trampoline

process_entry_trampoline:
    ldmia sp!, {r0-r12, lr}
    cps #0x1F              @ SYS mode (shares SP with USR)
    mov sp, r0             @ set user stack pointer
    cps #0x13              @ back to SVC
    rfeia sp!

.macro exception_entry, lr_adjust
    sub lr, lr, #\lr_adjust
    srsdb sp!, #0x13          @ push return_pc + cpsr to SVC stack
    cpsid i, #0x13            @ switch to SVC mode
    stmdb sp!, {r0-r12, lr}  @ push r0-r12, lr_svc
.endm

.macro exception_exit
    ldmia sp!, {r0-r12, lr}
    rfeia sp!
.endm

reset_handler:
    cpsid if             @ Mask interrupts
    cps #0x13            @ SVC mode

    b _start             @ Go to startup code
1:
    b 1b

irq_handler:
    exception_entry 4
    mov r0, #6
    mov r1, sp
    bl exception_dispatch
    exception_exit

reserved_handler:
    exception_entry 4
    mov r0, #5
    mov r1, sp
    bl exception_dispatch
    exception_exit

data_abort_handler:
    exception_entry 8
    mov r0, #4
    mov r1, sp
    bl exception_dispatch
    exception_exit

svc_handler:
    exception_entry 0
    mov r0, #2
    mov r1, sp
    bl exception_dispatch
    exception_exit

prefetch_abort_handler:
    exception_entry 4
    mov r0, #3
    mov r1, sp
    bl exception_dispatch
    exception_exit

undef_handler:
    exception_entry 4
    mov r0, #1
    mov r1, sp
    bl exception_dispatch
    exception_exit

fiq_handler:
    exception_entry 4
    mov r0, #7
    mov r1, sp
    bl exception_dispatch
    exception_exit
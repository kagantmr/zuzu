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
.global task_entry_trampoline

.extern do_resched

task_entry_trampoline:

    add r0, sp, #52
    ldm r0, {r13, r14}^
    nop
    ldmia sp!, {r0-r12}
    add sp, sp, #8
    rfeia sp!

.macro exception_entry, lr_adjust
    sub lr, lr, #\lr_adjust
    srsdb sp!, #0x13
    cpsid i, #0x13
    sub sp, sp, #8
    stmdb sp!, {r0-r12}
    add r0, sp, #52
    stm r0, {r13, r14}^
    nop
.endm

.macro exception_exit
    add r0, sp, #52
    ldm r0, {r13, r14}^
    nop
    ldmia sp!, {r0-r12}
    add sp, sp, #8
    rfeia sp!
.endm

.macro exception_dispatch_call, type
    mov r0, #\type
    mov r1, sp
    mov r4, sp
    bic sp, sp, #7
    bl ExceptionDispatch
    mov sp, r4
.endm

reset_handler:
    cpsid if
    cps #0x13

    b _start
1:
    b 1b

irq_handler:
    exception_entry 4
    exception_dispatch_call 6
    ldr r0, =do_resched
    ldrb r1, [r0]
    cmp r1, #0
    beq .no_schedule
.do_schedule:
    mov r1, #0
    strb r1, [r0]
    mov r4, sp
    bic sp, sp, #7
    bl Schedule
    mov sp, r4
.no_schedule:
    exception_exit

reserved_handler:
    exception_entry 4
    exception_dispatch_call 5
    exception_exit

data_abort_handler:
    exception_entry 8
    exception_dispatch_call 4
    exception_exit

svc_handler:
    exception_entry 0
    mov r0, sp
    mov r4, sp
    bic sp, sp, #7
    bl SvcTrap
    mov sp, r4
    ldr r0, =do_resched
    ldrb r1, [r0]
    cmp r1, #0
    beq .svc_no_schedule
    mov r1, #0
    strb r1, [r0]
    mov r4, sp
    bic sp, sp, #7
    bl Schedule
    mov sp, r4
.svc_no_schedule:
    exception_exit

prefetch_abort_handler:
    exception_entry 4
    exception_dispatch_call 3
    exception_exit

undef_handler:
    exception_entry 4
    exception_dispatch_call 1
    exception_exit

fiq_handler:
    exception_entry 4
    exception_dispatch_call 7
    exception_exit

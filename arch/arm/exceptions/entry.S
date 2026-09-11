// entry.S - exception entry points and trampoline for process entry
// Assembly code for the exception entry points for ARMv7. 
// Basic handlers for exceptions, they route to exception_dispatch. 

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

.extern do_resched

process_entry_trampoline:
    // frame at SP: r0-r12, sp_usr, lr_usr, return_pc, return_cpsr

    add r0, sp, #52
    ldm r0, {r13, r14}^     // loads frame[52]->r13_usr, frame[56]->r14_usr, no mode change

    ldmia sp!, {r0-r12}        // restore regs (13 words)
    add sp, sp, #8             // skip sp_usr/lr_usr
    rfeia sp!                  // pop return_pc + return_cpsr into PC/CPSR

.macro exception_entry, lr_adjust
    sub lr, lr, #\lr_adjust
    srsdb sp!, #0x13          // push [return_pc, cpsr]
    cpsid i, #0x13            // SVC, IRQs off
    sub sp, sp, #8            // reserve [sp_usr, lr_usr]
    stmdb sp!, {r0-r12}       // push r0-r12
    // frame is now complete except sp_usr/lr_usr
    add r0, sp, #52
    stm r0, {r13, r14}^       // stores r13_usr -> [r0], r14_usr -> [r0+4], no mode change
.endm

.macro exception_exit
    ldr r0, [sp, #60]         // return_pc
    cmp r0, #0
    bne 8f
    mov r0, sp
    bl exception_exit_pc0_trap
8:
    add r0, sp, #52
    ldm r0, {r13, r14}^       // loads frame[52]->r13_usr, frame[56]->r14_usr, no mode change

    ldmia sp!, {r0-r12}       // restore r0-r12
    add sp, sp, #8            // skip sp_usr/lr_usr
    rfeia sp!                 // restore pc + cpsr
.endm

.macro exception_dispatch_call, type
    mov r0, #\type
    mov r1, sp
    sub sp, sp, #4            // AAPCS: keep 8-byte alignment at C call boundary
    bl exception_dispatch
    add sp, sp, #4
.endm

reset_handler:
    cpsid if             // Mask interrupts
    cps #0x13            // SVC mode

    b _start             // Go to startup code
1:
    b 1b

irq_handler:
    exception_entry 4
    exception_dispatch_call 6
    // Check flag to see if we need to call the scheduler
    ldr r0, =do_resched
    ldrb r1, [r0]
    cmp r1, #0
    beq .no_schedule
.do_schedule:
    mov r1, #0
    strb r1, [r0]          // Clear flag
    sub sp, sp, #4         // AAPCS: keep 8-byte alignment at C call boundary
    bl Schedule
    add sp, sp, #4
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
    exception_dispatch_call 2
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

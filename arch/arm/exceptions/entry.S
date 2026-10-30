/**
 * @file
 * @brief ARMv7-A exception entry and exit: builds the CpuState trap frame on the SVC stack.
 */

.arch armv7ve
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

/** Set by the scheduler when a reschedule is due on the way back to the interrupted task. */
.extern do_resched

/**
 * task_entry_trampoline
 *
 * Where a new user task first runs: ArchTaskUserInit() makes its CpuContext
 * return here, with sp at the CpuState it prepared. Loads that frame and
 * enters user mode, like exception_exit.
 */
task_entry_trampoline:

    add r0, sp, #52
    ldm r0, {r13, r14}^
    nop
    ldmia sp!, {r0-r12}
    add sp, sp, #8
    rfeia sp!

/**
 * exception_entry lr_adjust
 *
 * Turns the exception-mode lr into the return address (lr - lr_adjust), saves it
 * and the SPSR on the SVC stack (srsdb), switches to SVC mode with IRQs masked
 * and pushes the rest of a CpuState: r0-r12, then the user sp and lr. On exit
 * sp points at the CpuState. Clobbers r0.
 */
 .macro exception_entry, lr_adjust
     sub lr, lr, #\lr_adjust
     srsdb sp!, #0x13
     cpsid i, #0x13
     sub sp, sp, #8
     stmdb sp!, {r0-r12}
     add r0, sp, #52
     mrs r1, SP_usr
     mrs r2, LR_usr
     stm r0, {r1, r2}
 .endm
 
 .macro svc_entry
     srsdb sp!, #0x13
     sub sp, sp, #8
     stmdb sp!, {r0-r12}
     add r0, sp, #52
     mrs r1, SP_usr
     mrs r2, LR_usr
     stm r0, {r1, r2}
 .endm
 
 .macro exception_exit
     add r0, sp, #52
     ldm r0, {r1, r2}
     msr SP_usr, r1
     msr LR_usr, r2
     ldmia sp!, {r0-r12}
     add sp, sp, #8
     rfeia sp!
 .endm

/**
 * exception_dispatch_call type
 *
 * Calls ExceptionDispatch(type, frame) with sp aligned to 8 bytes as the AAPCS
 * requires; r4 keeps the unaligned sp. type is an ExceptionType.
 */
.macro exception_dispatch_call, type
    mov r0, #\type
    mov r1, sp
    mov r4, sp
    bic sp, sp, #7
    bl ExceptionDispatch
    mov sp, r4
.endm

/** Reset vector: masks interrupts, enters SVC mode and restarts the boot code. */
reset_handler:
    cpsid if
    cps #0x13

    b _start
1:
    b 1b

/** IRQ vector: dispatches the interrupt, then reschedules if do_resched was set. */
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

/** Unused vector; ExceptionDispatch() panics. */
reserved_handler:
    exception_entry 4
    exception_dispatch_call 5
    exception_exit

/** Data abort vector. lr is 8 past the faulting instruction, so returning retries it. */
data_abort_handler:
    exception_entry 8
    exception_dispatch_call 4
    exception_exit

/**
 * SVC vector: SvcTrap() runs the syscall on the CpuState at sp, then the task
 * is rescheduled if do_resched was set. Returns to the instruction after svc.
 */
svc_handler:
    svc_entry
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

/** Prefetch abort vector. */
prefetch_abort_handler:
    exception_entry 4
    exception_dispatch_call 3
    exception_exit

/**
 * Undefined instruction vector. Returning re-executes the instruction, which is
 * what the lazy VFP switch wants once access has been granted.
 */
undef_handler:
    exception_entry 4
    exception_dispatch_call 1
    exception_exit

/** FIQ vector; FIQs are not used. */
fiq_handler:
    exception_entry 4
    exception_dispatch_call 7
    exception_exit

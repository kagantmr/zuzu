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

reset_handler:
    cpsid if             @ Mask interrupts
    cps #0x13            @ SVC mode

    b _start             @ Go to startup code
1:
    b 1b

undef_handler:
    stmdb sp!, {lr}        @ Save original LR

    sub lr, lr, #4 @ PC = LR - 4 for undefined instruction

    /* r0–r12 and original lr */
    stmdb sp!, {r0-r12, lr}

    /* spsr */
    mrs r0, spsr
    stmdb sp!, {r0}

    mov r1, sp              @ pointer to struct
    mov r0, #1              @ EXC_UNDEF
    bl exception_dispatch   @ call exception_dispatch(enum exc_type, exception_frame *frame)

    ldmia sp!, {r0}         @ Restore spsr into r0
    msr spsr_cxsf, r0       

    ldmia sp!, {r0-r12, lr} 
    ldmia sp!, {lr}         @ Restore pc

    subs pc, lr, #0         @ Return from exception
svc_handler:

    stmdb sp!, {lr}        @ Save original LR

    sub lr, lr, #4 @ PC = LR - 4 for supervisor call

    /* r0–r12 and original lr */
    stmdb sp!, {r0-r12, lr}

    /* spsr */
    mrs r0, spsr
    stmdb sp!, {r0}

    mov r1, sp              @ pointer to struct
    mov r0, #2              @ EXC_SVC
    bl exception_dispatch   @ call exception_dispatch(enum exc_type, exception_frame *frame)

    ldmia sp!, {r0}         @ Restore spsr into r0
    msr spsr_cxsf, r0       

    ldmia sp!, {r0-r12, lr} 
    ldmia sp!, {lr}         @ Restore pc

    subs pc, lr, #0         @ Return from exception
prefetch_abort_handler:

    stmdb sp!, {lr}        @ Save original LR

    sub lr, lr, #4 @ PC = LR - 4 for prefetch abort

    /* r0–r12 and original lr */
    stmdb sp!, {r0-r12, lr}

    /* spsr */
    mrs r0, spsr
    stmdb sp!, {r0}

    mov r1, sp              @ pointer to struct
    mov r0, #3              @ EXC_PREFETCH_ABORT
    bl exception_dispatch   @ call exception_dispatch(enum exc_type, exception_frame *frame)

    ldmia sp!, {r0}         @ Restore spsr into r0
    msr spsr_cxsf, r0       

    ldmia sp!, {r0-r12, lr} 
    ldmia sp!, {lr}         @ Restore pc

    subs pc, lr, #0         @ Return from exception
data_abort_handler:

    stmdb sp!, {lr}        @ Save original LR

    sub lr, lr, #8 @ PC = LR - 8 for data abort

    /* r0–r12 and original lr */
    stmdb sp!, {r0-r12, lr}

    /* spsr */
    mrs r0, spsr
    stmdb sp!, {r0}

    mov r1, sp              @ pointer to struct
    mov r0, #4              @ EXC_DATA_ABORT
    bl exception_dispatch   @ call exception_dispatch(enum exc_type, exception_frame *frame)

    ldmia sp!, {r0}         @ Restore spsr into r0
    msr spsr_cxsf, r0       

    ldmia sp!, {r0-r12, lr} 
    ldmia sp!, {lr}         @ Restore pc

    subs pc, lr, #0         @ Return from exception
reserved_handler:

    stmdb sp!, {lr}        @ Save original LR

    sub lr, lr, #4 @ PC = LR - 4 for reserved

    /* r0–r12 and original lr */
    stmdb sp!, {r0-r12, lr}

    /* spsr */
    mrs r0, spsr
    stmdb sp!, {r0}

    mov r1, sp              @ pointer to struct
    mov r0, #5              @ EXC_RESERVED
    bl exception_dispatch   @ call exception_dispatch(enum exc_type, exception_frame *frame)

    ldmia sp!, {r0}         @ Restore spsr into r0
    msr spsr_cxsf, r0       

    ldmia sp!, {r0-r12, lr} 
    ldmia sp!, {lr}         @ Restore pc

    subs pc, lr, #0         @ Return from exception
irq_handler:
    @ Adjust return address (standard IRQ linkage)
    sub lr, lr, #4
    
    @ Store Return State (lr_irq and spsr_irq) to SVC mode's stack
    @ This magically pushes to sp_svc, not sp_irq!
    srsdb sp!, #0x13
    
    @ Switch to SVC mode, IRQs stay disabled
    cpsid i, #0x13
    
    @ Now sp = sp_svc (the interrupted process's kernel stack)
    @ Save all general purpose registers
    stmdb sp!, {r0-r12, lr}
    
    @ Call C handler - may call schedule() -> context_switch()
    mov r0, #6           @ EXC_IRQ = 6
    mov r1, sp           @ frame pointer
    bl exception_dispatch
    
    @ After return, sp may point to a DIFFERENT process's stack!
    @ Restore all registers
    ldmia sp!, {r0-r12, lr}
    
    @ Return From Exception - pops pc and cpsr, returns to (possibly different) process
    rfeia sp!
fiq_handler:

    stmdb sp!, {lr}        @ Save original LR

    sub lr, lr, #4 @ PC = LR - 4 for FIQ

    /* r0–r12 and original lr */
    stmdb sp!, {r0-r12, lr}

    /* spsr */
    mrs r0, spsr
    stmdb sp!, {r0}

    mov r1, sp              @ pointer to struct
    mov r0, #7              @ EXC_FIQ
    bl exception_dispatch   @ call exception_dispatch(enum exc_type, exception_frame *frame)

    ldmia sp!, {r0}         @ Restore spsr into r0
    msr spsr_cxsf, r0       

    ldmia sp!, {r0-r12, lr} 
    ldmia sp!, {lr}         @ Restore pc

    subs pc, lr, #0         @ Return from exception


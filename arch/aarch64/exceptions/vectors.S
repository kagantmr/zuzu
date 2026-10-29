/**
 * @file
 * @brief ARMv7-A exception vector table; VBAR points here once the kernel runs in the higher half.
 */

/** 32-byte aligned, as VBAR requires. One branch per exception, in hardware order. */
.section .vectors, "ax"
.align 5
.global vector_table

vector_table:
    b reset_handler
    b undef_handler
    b svc_handler
    b prefetch_abort_handler
    b data_abort_handler
    b reserved_handler
    b irq_handler
    b fiq_handler

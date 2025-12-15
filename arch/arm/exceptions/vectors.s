.section .vectors, "ax"
.align 5        @ 2^5 = 32-byte alignment
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
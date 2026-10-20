.arch armv7-a

.arm
.align 2
.p2align 4,,15
.global memset
.type memset,%function

memset:

    push {r4-r11}
    mov ip, r0
copy_align_to_word:
    and r3, r0, #3
    cmp r3, #0
    beq prepare_fill_word

    cmp r2, #0
    beq done

    strb r1, [r0], #1
    
    sub r2, r2, #1
    b copy_align_to_word

prepare_fill_word:
    and r1, r1, #0xFF
    orr r1, r1, r1, lsl #8
    orr r1, r1, r1, lsl #16
    mov r4, r1
    mov r5, r1
    mov r6, r1
    mov r7, r1
    mov r8, r1
    mov r9, r1
    mov r10, r1
    mov r11, r1
stmia_loop:
    cmp r2, #32
    blt str_loop

    stmia r0!, {r4-r11}

    sub r2, r2, #32
    b stmia_loop
str_loop:
    cmp r2, #4
    blt strb_loop

    str r1, [r0], #4

    sub r2, r2, #4
    b str_loop
strb_loop:
    cmp r2, #0
    beq done

    strb r1, [r0], #1

    sub r2, r2, #1
    b strb_loop
done:
    mov r0, ip
    pop {r4-r11}
    bx lr

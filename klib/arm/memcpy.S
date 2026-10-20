.arch armv7-a

.arm
.align 2
.p2align 4,,15
.global memcpy
.type memcpy,%function

memcpy:
    push {r4-r11}           

    mov  ip, r0              

    eor  r3, r0, r1
    ands r3, r3, #3
    bne  slow_word_loop

align_loop:
    ands r3, r0, #3
    beq  bulk_loop
    cmp  r2, #0
    beq  done
    ldrb r3, [r1], #1
    strb r3, [r0], #1
    sub  r2, r2, #1
    b    align_loop

bulk_loop:
    cmp  r2, #32
    blt  word_loop
    pld  [r1, #128]
    ldmia r1!, {r4-r11}
    stmia r0!, {r4-r11}
    sub  r2, r2, #32
    b    bulk_loop

word_loop:
    cmp  r2, #4
    blt  byte_loop
    ldr  r3, [r1], #4
    str  r3, [r0], #4
    sub  r2, r2, #4
    b    word_loop

byte_loop:
    cmp  r2, #0
    beq  done
    ldrb r3, [r1], #1
    strb r3, [r0], #1
    sub  r2, r2, #1
    b    byte_loop

slow_word_loop:
    cmp  r2, #4
    blt  slow_byte_loop
    ldr  r3, [r1], #4
    str  r3, [r0], #4
    sub  r2, r2, #4
    b    slow_word_loop

slow_byte_loop:
    cmp  r2, #0
    beq  done
    ldrb r3, [r1], #1
    strb r3, [r0], #1
    sub  r2, r2, #1
    b    slow_byte_loop

done:
    mov  r0, ip
    pop  {r4-r11}
    bx   lr

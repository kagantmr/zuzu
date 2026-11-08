/**
 * @file
 * @brief AArch64 memcpy().
 */

.text
.balign 16
.global memcpy
.type memcpy, %function

/**
 * memcpy(void *dest, const void *src, size_t n)
 * In:       x0 = dest, x1 = src, x2 = n.
 * Out:      x0 = dest.
 * Clobbers: x1-x11.
 */
memcpy:
    mov     x3, x0
    neg     x4, x3
    and     x4, x4, #15
    cmp     x4, x2
    csel    x4, x4, x2, lo
    cbz     x4, .Lcpy_aligned
    sub     x2, x2, x4
.Lcpy_head:
    ldrb    w5, [x1], #1
    strb    w5, [x3], #1
    subs    x4, x4, #1
    b.ne    .Lcpy_head

.Lcpy_aligned:
    cmp     x2, #64
    b.lo    .Lcpy_16
.Lcpy_64:
    prfm    pldl1strm, [x1, #256]
    ldp     x4, x5, [x1]
    ldp     x6, x7, [x1, #16]
    ldp     x8, x9, [x1, #32]
    ldp     x10, x11, [x1, #48]
    add     x1, x1, #64
    stp     x4, x5, [x3]
    stp     x6, x7, [x3, #16]
    stp     x8, x9, [x3, #32]
    stp     x10, x11, [x3, #48]
    add     x3, x3, #64
    sub     x2, x2, #64
    cmp     x2, #64
    b.hs    .Lcpy_64

.Lcpy_16:
    cmp     x2, #16
    b.lo    .Lcpy_tail
    ldp     x4, x5, [x1], #16
    stp     x4, x5, [x3], #16
    sub     x2, x2, #16
    b       .Lcpy_16

.Lcpy_tail:
    tbz     x2, #3, 1f
    ldr     x4, [x1], #8
    str     x4, [x3], #8
1:  tbz     x2, #2, 2f
    ldr     w4, [x1], #4
    str     w4, [x3], #4
2:  tbz     x2, #1, 3f
    ldrh    w4, [x1], #2
    strh    w4, [x3], #2
3:  tbz     x2, #0, 4f
    ldrb    w4, [x1]
    strb    w4, [x3]
4:  ret
.size memcpy, . - memcpy

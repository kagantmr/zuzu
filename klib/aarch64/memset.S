/**
 * @file
 * @brief AArch64 memset(): aligns the destination to 16 bytes, then stores 64-byte blocks with
 * stp. General-purpose registers only, so it is safe in the kernel.
 */

.text
.balign 16
.global memset
.type memset, %function

/**
 * memset(void *dst, int c, size_t n)
 * In:       x0 = dst, w1 = c, x2 = n.
 * Out:      x0 = dst.
 * Clobbers: x1-x4.
 */
memset:
    mov     x3, x0
    and     x1, x1, #0xff
    mov     x4, #0x0101010101010101
    mul     x1, x1, x4

    neg     x4, x3
    and     x4, x4, #15
    cmp     x4, x2
    csel    x4, x4, x2, lo
    cbz     x4, .Lset_aligned
    sub     x2, x2, x4
.Lset_head:
    strb    w1, [x3], #1
    subs    x4, x4, #1
    b.ne    .Lset_head

.Lset_aligned:
    cmp     x2, #64
    b.lo    .Lset_16
.Lset_64:
    stp     x1, x1, [x3]
    stp     x1, x1, [x3, #16]
    stp     x1, x1, [x3, #32]
    stp     x1, x1, [x3, #48]
    add     x3, x3, #64
    sub     x2, x2, #64
    cmp     x2, #64
    b.hs    .Lset_64

.Lset_16:
    cmp     x2, #16
    b.lo    .Lset_tail
    stp     x1, x1, [x3], #16
    sub     x2, x2, #16
    b       .Lset_16

.Lset_tail:
    tbz     x2, #3, 1f
    str     x1, [x3], #8
1:  tbz     x2, #2, 2f
    str     w1, [x3], #4
2:  tbz     x2, #1, 3f
    strh    w1, [x3], #2
3:  tbz     x2, #0, 4f
    strb    w1, [x3]
4:  ret
.size memset, . - memset

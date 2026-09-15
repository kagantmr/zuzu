.arch armv7-a

.arm
.align 2
.p2align 4,,15
.global memcpy
.type memcpy,%function

/*
 * void *memcpy(void *dst, const void *src, size_t n)
 * AAPCS in:  r0 = dst, r1 = src, r2 = n
 * AAPCS out: r0 = dst (memcpy returns the original destination pointer)
 *
 * No NEON here on purpose: this runs in the kernel, which is soft-float,
 * so there's no live VFP/NEON register file to use even if we wanted to.
 */
memcpy:
    push {r4-r11}           /* r4-r11 are callee-saved (AAPCS) and we use
                              * all 8 as a scratch block for the bulk copy,
                              * so stash the caller's values first */
    mov  ip, r0              /* r0 becomes our moving write pointer below,
                              * so save the original dst (this is what we
                              * have to return) in ip -- caller-saved, ours
                              * to clobber freely */

    /* ldm/stm (used in the bulk loop below) require the address to be
     * word-aligned, if not go straight to the slower
     * word-at-a-time path instead. */
    eor  r3, r0, r1
    ands r3, r3, #3
    bne  slow_word_loop

    /* ---- fast path: dst and src agree mod 4 ---- */

    /* Walk both pointers forward one byte at a time until dst (and so
     * src too, same residue) lands on a word boundary. */
align_loop:
    ands r3, r0, #3
    beq  bulk_loop
    cmp  r2, #0
    beq  done
    ldrb r3, [r1], #1
    strb r3, [r0], #1
    sub  r2, r2, #1
    b    align_loop

    /* Both pointers are word-aligned now. Move 32 bytes/iteration: load
     * 8 words from src into r4-r11, then store all 8 to dst. A pld two
     * cache lines ahead of the read pointer overlaps the next chunk's
     * fetch with the current chunk's store instead of stalling for it. */
bulk_loop:
    cmp  r2, #32
    blt  word_loop
    pld  [r1, #128]
    ldmia r1!, {r4-r11}
    stmia r0!, {r4-r11}
    sub  r2, r2, #32
    b    bulk_loop

    /* Remainder too small for another 32-byte block: mop up 4 bytes at
     * a time. Still safe to use ldr/str here since we're still aligned. */
word_loop:
    cmp  r2, #4
    blt  byte_loop
    ldr  r3, [r1], #4
    str  r3, [r0], #4
    sub  r2, r2, #4
    b    word_loop

    /* Final 0-3 bytes, one at a time. */
byte_loop:
    cmp  r2, #0
    beq  done
    ldrb r3, [r1], #1
    strb r3, [r0], #1
    sub  r2, r2, #1
    b    byte_loop

    /* ---- slow path: dst and src don't agree mod 4 ---- */
    /* Can't use ldm/stm at all here (see the comment above the branch
     * into this path)*/
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
    mov  r0, ip              /* restore the return value: original dst */
    pop  {r4-r11}
    bx   lr

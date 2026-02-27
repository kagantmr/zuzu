/*
 * initrd.s: Embed the CPIO initrd archive into the kernel image.
 * Size in C:  size_t sz = (size_t)(_initrd_end - _initrd_start);
 */

.section .rodata.initrd, "a", %progbits
.align 4

.global _initrd_start
_initrd_start:
    .incbin "build/initrd.cpio"

.global _initrd_end
_initrd_end:
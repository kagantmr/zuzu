@ initrd.S - Embedded initrd image for ARMv7-A

.section .rodata.initrd, "a", %progbits
.align 4

.global _initrd_start
_initrd_start:
    .incbin "build/initrd.cpio"

.global _initrd_end
_initrd_end:
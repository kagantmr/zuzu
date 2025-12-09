
build/zuzu.elf:     file format elf32-littlearm


Disassembly of section .text:

40008000 <uart_tx_full>:
40008000:	e52db004 	push	{fp}		@ (str fp, [sp, #-4]!)
40008004:	e28db000 	add	fp, sp, #0
40008008:	e24dd00c 	sub	sp, sp, #12
4000800c:	e30b3aa0 	movw	r3, #47776	@ 0xbaa0
40008010:	e3443000 	movt	r3, #16384	@ 0x4000
40008014:	e5933000 	ldr	r3, [r3]
40008018:	e2833018 	add	r3, r3, #24
4000801c:	e50b3008 	str	r3, [fp, #-8]
40008020:	e51b3008 	ldr	r3, [fp, #-8]
40008024:	e5933000 	ldr	r3, [r3]
40008028:	e2033020 	and	r3, r3, #32
4000802c:	e7e032d3 	ubfx	r3, r3, #5, #1
40008030:	e6ef3073 	uxtb	r3, r3
40008034:	e1a00003 	mov	r0, r3
40008038:	e28bd000 	add	sp, fp, #0
4000803c:	e49db004 	pop	{fp}		@ (ldr fp, [sp], #4)
40008040:	e12fff1e 	bx	lr

40008044 <uart_init>:
40008044:	e52db004 	push	{fp}		@ (str fp, [sp, #-4]!)
40008048:	e28db000 	add	fp, sp, #0
4000804c:	e24dd00c 	sub	sp, sp, #12
40008050:	e50b0008 	str	r0, [fp, #-8]
40008054:	e30b3aa0 	movw	r3, #47776	@ 0xbaa0
40008058:	e3443000 	movt	r3, #16384	@ 0x4000
4000805c:	e51b2008 	ldr	r2, [fp, #-8]
40008060:	e5832000 	str	r2, [r3]
40008064:	e320f000 	nop	{0}
40008068:	e28bd000 	add	sp, fp, #0
4000806c:	e49db004 	pop	{fp}		@ (ldr fp, [sp], #4)
40008070:	e12fff1e 	bx	lr

40008074 <uart_putc>:
40008074:	e52db008 	str	fp, [sp, #-8]!
40008078:	e58de004 	str	lr, [sp, #4]
4000807c:	e28db004 	add	fp, sp, #4
40008080:	e24dd010 	sub	sp, sp, #16
40008084:	e1a03000 	mov	r3, r0
40008088:	e54b300d 	strb	r3, [fp, #-13]
4000808c:	e30b3aa0 	movw	r3, #47776	@ 0xbaa0
40008090:	e3443000 	movt	r3, #16384	@ 0x4000
40008094:	e5933000 	ldr	r3, [r3]
40008098:	e50b3008 	str	r3, [fp, #-8]
4000809c:	e320f000 	nop	{0}
400080a0:	ebffffd6 	bl	40008000 <uart_tx_full>
400080a4:	e1a03000 	mov	r3, r0
400080a8:	e3530000 	cmp	r3, #0
400080ac:	1afffffb 	bne	400080a0 <uart_putc+0x2c>
400080b0:	e55b200d 	ldrb	r2, [fp, #-13]
400080b4:	e51b3008 	ldr	r3, [fp, #-8]
400080b8:	e5832000 	str	r2, [r3]
400080bc:	e320f000 	nop	{0}
400080c0:	e24bd004 	sub	sp, fp, #4
400080c4:	e59db000 	ldr	fp, [sp]
400080c8:	e28dd004 	add	sp, sp, #4
400080cc:	e49df004 	pop	{pc}		@ (ldr pc, [sp], #4)

400080d0 <uart_puts>:
400080d0:	e52db008 	str	fp, [sp, #-8]!
400080d4:	e58de004 	str	lr, [sp, #4]
400080d8:	e28db004 	add	fp, sp, #4
400080dc:	e24dd008 	sub	sp, sp, #8
400080e0:	e50b0008 	str	r0, [fp, #-8]
400080e4:	ea000006 	b	40008104 <uart_puts+0x34>
400080e8:	e51b3008 	ldr	r3, [fp, #-8]
400080ec:	e5d33000 	ldrb	r3, [r3]
400080f0:	e1a00003 	mov	r0, r3
400080f4:	ebffffde 	bl	40008074 <uart_putc>
400080f8:	e51b3008 	ldr	r3, [fp, #-8]
400080fc:	e2833001 	add	r3, r3, #1
40008100:	e50b3008 	str	r3, [fp, #-8]
40008104:	e51b3008 	ldr	r3, [fp, #-8]
40008108:	e5d33000 	ldrb	r3, [r3]
4000810c:	e3530000 	cmp	r3, #0
40008110:	1afffff4 	bne	400080e8 <uart_puts+0x18>
40008114:	e3a03000 	mov	r3, #0
40008118:	e1a00003 	mov	r0, r3
4000811c:	e24bd004 	sub	sp, fp, #4
40008120:	e59db000 	ldr	fp, [sp]
40008124:	e28dd004 	add	sp, sp, #4
40008128:	e49df004 	pop	{pc}		@ (ldr pc, [sp], #4)

4000812c <uart_printf>:
4000812c:	e92d000f 	push	{r0, r1, r2, r3}
40008130:	e52db008 	str	fp, [sp, #-8]!
40008134:	e58de004 	str	lr, [sp, #4]
40008138:	e28db004 	add	fp, sp, #4
4000813c:	e24dd008 	sub	sp, sp, #8
40008140:	e28b3008 	add	r3, fp, #8
40008144:	e50b3008 	str	r3, [fp, #-8]
40008148:	e51b2008 	ldr	r2, [fp, #-8]
4000814c:	e59b1004 	ldr	r1, [fp, #4]
40008150:	e3080074 	movw	r0, #32884	@ 0x8074
40008154:	e3440000 	movt	r0, #16384	@ 0x4000
40008158:	eb00035a 	bl	40008ec8 <vstrfmt>
4000815c:	e3a03000 	mov	r3, #0
40008160:	e1a00003 	mov	r0, r3
40008164:	e24bd004 	sub	sp, fp, #4
40008168:	e59db000 	ldr	fp, [sp]
4000816c:	e59de004 	ldr	lr, [sp, #4]
40008170:	e28dd008 	add	sp, sp, #8
40008174:	e28dd010 	add	sp, sp, #16
40008178:	e12fff1e 	bx	lr

4000817c <panic>:
4000817c:	e52db008 	str	fp, [sp, #-8]!
40008180:	e58de004 	str	lr, [sp, #4]
40008184:	e28db004 	add	fp, sp, #4
40008188:	e24dd008 	sub	sp, sp, #8
4000818c:	e50b0008 	str	r0, [fp, #-8]
40008190:	eb000066 	bl	40008330 <disable_interrupts>
40008194:	e51b0008 	ldr	r0, [fp, #-8]
40008198:	ebffffcc 	bl	400080d0 <uart_puts>
4000819c:	e30b00c4 	movw	r0, #45252	@ 0xb0c4
400081a0:	e3440000 	movt	r0, #16384	@ 0x4000
400081a4:	ebffffc9 	bl	400080d0 <uart_puts>
400081a8:	e320f003 	wfi
400081ac:	eafffffd 	b	400081a8 <panic+0x2c>
400081b0:	e320f000 	nop	{0}
400081b4:	e24bd004 	sub	sp, fp, #4
400081b8:	e59db000 	ldr	fp, [sp]
400081bc:	e28dd004 	add	sp, sp, #4
400081c0:	e49df004 	pop	{pc}		@ (ldr pc, [sp], #4)

400081c4 <panicbuf_putc>:
400081c4:	e52db004 	push	{fp}		@ (str fp, [sp, #-4]!)
400081c8:	e28db000 	add	fp, sp, #0
400081cc:	e24dd00c 	sub	sp, sp, #12
400081d0:	e1a03000 	mov	r3, r0
400081d4:	e54b3005 	strb	r3, [fp, #-5]
400081d8:	e30b3ba4 	movw	r3, #48036	@ 0xbba4
400081dc:	e3443000 	movt	r3, #16384	@ 0x4000
400081e0:	e5933000 	ldr	r3, [r3]
400081e4:	e35300fe 	cmp	r3, #254	@ 0xfe
400081e8:	8a00000a 	bhi	40008218 <panicbuf_putc+0x54>
400081ec:	e30b3ba4 	movw	r3, #48036	@ 0xbba4
400081f0:	e3443000 	movt	r3, #16384	@ 0x4000
400081f4:	e5932000 	ldr	r2, [r3]
400081f8:	e2821001 	add	r1, r2, #1
400081fc:	e30b3ba4 	movw	r3, #48036	@ 0xbba4
40008200:	e3443000 	movt	r3, #16384	@ 0x4000
40008204:	e5831000 	str	r1, [r3]
40008208:	e30b3aa4 	movw	r3, #47780	@ 0xbaa4
4000820c:	e3443000 	movt	r3, #16384	@ 0x4000
40008210:	e55b1005 	ldrb	r1, [fp, #-5]
40008214:	e7c31002 	strb	r1, [r3, r2]
40008218:	e320f000 	nop	{0}
4000821c:	e28bd000 	add	sp, fp, #0
40008220:	e49db004 	pop	{fp}		@ (ldr fp, [sp], #4)
40008224:	e12fff1e 	bx	lr

40008228 <panicf>:
40008228:	e92d000f 	push	{r0, r1, r2, r3}
4000822c:	e52db008 	str	fp, [sp, #-8]!
40008230:	e58de004 	str	lr, [sp, #4]
40008234:	e28db004 	add	fp, sp, #4
40008238:	e24dd008 	sub	sp, sp, #8
4000823c:	e30b3ba4 	movw	r3, #48036	@ 0xbba4
40008240:	e3443000 	movt	r3, #16384	@ 0x4000
40008244:	e3a02000 	mov	r2, #0
40008248:	e5832000 	str	r2, [r3]
4000824c:	e28b3008 	add	r3, fp, #8
40008250:	e50b3008 	str	r3, [fp, #-8]
40008254:	e51b2008 	ldr	r2, [fp, #-8]
40008258:	e59b1004 	ldr	r1, [fp, #4]
4000825c:	e30801c4 	movw	r0, #33220	@ 0x81c4
40008260:	e3440000 	movt	r0, #16384	@ 0x4000
40008264:	eb000317 	bl	40008ec8 <vstrfmt>
40008268:	e30b3ba4 	movw	r3, #48036	@ 0xbba4
4000826c:	e3443000 	movt	r3, #16384	@ 0x4000
40008270:	e5932000 	ldr	r2, [r3]
40008274:	e30b3aa4 	movw	r3, #47780	@ 0xbaa4
40008278:	e3443000 	movt	r3, #16384	@ 0x4000
4000827c:	e3a01000 	mov	r1, #0
40008280:	e7c31002 	strb	r1, [r3, r2]
40008284:	e30b0aa4 	movw	r0, #47780	@ 0xbaa4
40008288:	e3440000 	movt	r0, #16384	@ 0x4000
4000828c:	ebffffba 	bl	4000817c <panic>
40008290:	e320f000 	nop	{0}
40008294:	e24bd004 	sub	sp, fp, #4
40008298:	e59db000 	ldr	fp, [sp]
4000829c:	e59de004 	ldr	lr, [sp, #4]
400082a0:	e28dd008 	add	sp, sp, #8
400082a4:	e28dd010 	add	sp, sp, #16
400082a8:	e12fff1e 	bx	lr

400082ac <kprintf_init>:
400082ac:	e52db004 	push	{fp}		@ (str fp, [sp, #-4]!)
400082b0:	e28db000 	add	fp, sp, #0
400082b4:	e24dd00c 	sub	sp, sp, #12
400082b8:	e50b0008 	str	r0, [fp, #-8]
400082bc:	e30b3ba8 	movw	r3, #48040	@ 0xbba8
400082c0:	e3443000 	movt	r3, #16384	@ 0x4000
400082c4:	e51b2008 	ldr	r2, [fp, #-8]
400082c8:	e5832000 	str	r2, [r3]
400082cc:	e320f000 	nop	{0}
400082d0:	e28bd000 	add	sp, fp, #0
400082d4:	e49db004 	pop	{fp}		@ (ldr fp, [sp], #4)
400082d8:	e12fff1e 	bx	lr

400082dc <kprintf>:
400082dc:	e92d000f 	push	{r0, r1, r2, r3}
400082e0:	e52db008 	str	fp, [sp, #-8]!
400082e4:	e58de004 	str	lr, [sp, #4]
400082e8:	e28db004 	add	fp, sp, #4
400082ec:	e24dd008 	sub	sp, sp, #8
400082f0:	e28b3008 	add	r3, fp, #8
400082f4:	e50b3008 	str	r3, [fp, #-8]
400082f8:	e30b3ba8 	movw	r3, #48040	@ 0xbba8
400082fc:	e3443000 	movt	r3, #16384	@ 0x4000
40008300:	e5933000 	ldr	r3, [r3]
40008304:	e51b2008 	ldr	r2, [fp, #-8]
40008308:	e59b1004 	ldr	r1, [fp, #4]
4000830c:	e1a00003 	mov	r0, r3
40008310:	eb0002ec 	bl	40008ec8 <vstrfmt>
40008314:	e320f000 	nop	{0}
40008318:	e24bd004 	sub	sp, fp, #4
4000831c:	e59db000 	ldr	fp, [sp]
40008320:	e59de004 	ldr	lr, [sp, #4]
40008324:	e28dd008 	add	sp, sp, #8
40008328:	e28dd010 	add	sp, sp, #16
4000832c:	e12fff1e 	bx	lr

40008330 <disable_interrupts>:
40008330:	e52db004 	push	{fp}		@ (str fp, [sp, #-4]!)
40008334:	e28db000 	add	fp, sp, #0
40008338:	f10c0080 	cpsid	i
4000833c:	e320f000 	nop	{0}
40008340:	e28bd000 	add	sp, fp, #0
40008344:	e49db004 	pop	{fp}		@ (ldr fp, [sp], #4)
40008348:	e12fff1e 	bx	lr

4000834c <enable_interrupts>:
4000834c:	e52db004 	push	{fp}		@ (str fp, [sp, #-4]!)
40008350:	e28db000 	add	fp, sp, #0
40008354:	f1080080 	cpsie	i
40008358:	e320f000 	nop	{0}
4000835c:	e28bd000 	add	sp, fp, #0
40008360:	e49db004 	pop	{fp}		@ (ldr fp, [sp], #4)
40008364:	e12fff1e 	bx	lr

40008368 <memcpy>:
40008368:	e52db004 	push	{fp}		@ (str fp, [sp, #-4]!)
4000836c:	e28db000 	add	fp, sp, #0
40008370:	e24dd024 	sub	sp, sp, #36	@ 0x24
40008374:	e50b0018 	str	r0, [fp, #-24]	@ 0xffffffe8
40008378:	e50b101c 	str	r1, [fp, #-28]	@ 0xffffffe4
4000837c:	e50b2020 	str	r2, [fp, #-32]	@ 0xffffffe0
40008380:	e51b3018 	ldr	r3, [fp, #-24]	@ 0xffffffe8
40008384:	e50b3014 	str	r3, [fp, #-20]	@ 0xffffffec
40008388:	e51b3018 	ldr	r3, [fp, #-24]	@ 0xffffffe8
4000838c:	e50b3008 	str	r3, [fp, #-8]
40008390:	e51b301c 	ldr	r3, [fp, #-28]	@ 0xffffffe4
40008394:	e50b300c 	str	r3, [fp, #-12]
40008398:	e3a03000 	mov	r3, #0
4000839c:	e50b3010 	str	r3, [fp, #-16]
400083a0:	ea00000a 	b	400083d0 <memcpy+0x68>
400083a4:	e51b200c 	ldr	r2, [fp, #-12]
400083a8:	e2823001 	add	r3, r2, #1
400083ac:	e50b300c 	str	r3, [fp, #-12]
400083b0:	e51b3008 	ldr	r3, [fp, #-8]
400083b4:	e2831001 	add	r1, r3, #1
400083b8:	e50b1008 	str	r1, [fp, #-8]
400083bc:	e5d22000 	ldrb	r2, [r2]
400083c0:	e5c32000 	strb	r2, [r3]
400083c4:	e51b3010 	ldr	r3, [fp, #-16]
400083c8:	e2833001 	add	r3, r3, #1
400083cc:	e50b3010 	str	r3, [fp, #-16]
400083d0:	e51b2010 	ldr	r2, [fp, #-16]
400083d4:	e51b3020 	ldr	r3, [fp, #-32]	@ 0xffffffe0
400083d8:	e1520003 	cmp	r2, r3
400083dc:	3afffff0 	bcc	400083a4 <memcpy+0x3c>
400083e0:	e51b3014 	ldr	r3, [fp, #-20]	@ 0xffffffec
400083e4:	e1a00003 	mov	r0, r3
400083e8:	e28bd000 	add	sp, fp, #0
400083ec:	e49db004 	pop	{fp}		@ (ldr fp, [sp], #4)
400083f0:	e12fff1e 	bx	lr

400083f4 <memset>:
400083f4:	e52db004 	push	{fp}		@ (str fp, [sp, #-4]!)
400083f8:	e28db000 	add	fp, sp, #0
400083fc:	e24dd024 	sub	sp, sp, #36	@ 0x24
40008400:	e50b0018 	str	r0, [fp, #-24]	@ 0xffffffe8
40008404:	e1a03001 	mov	r3, r1
40008408:	e50b2020 	str	r2, [fp, #-32]	@ 0xffffffe0
4000840c:	e54b3019 	strb	r3, [fp, #-25]	@ 0xffffffe7
40008410:	e51b3018 	ldr	r3, [fp, #-24]	@ 0xffffffe8
40008414:	e50b3010 	str	r3, [fp, #-16]
40008418:	e51b3018 	ldr	r3, [fp, #-24]	@ 0xffffffe8
4000841c:	e50b3008 	str	r3, [fp, #-8]
40008420:	e3a03000 	mov	r3, #0
40008424:	e50b300c 	str	r3, [fp, #-12]
40008428:	ea000007 	b	4000844c <memset+0x58>
4000842c:	e51b3008 	ldr	r3, [fp, #-8]
40008430:	e2832001 	add	r2, r3, #1
40008434:	e50b2008 	str	r2, [fp, #-8]
40008438:	e55b2019 	ldrb	r2, [fp, #-25]	@ 0xffffffe7
4000843c:	e5c32000 	strb	r2, [r3]
40008440:	e51b300c 	ldr	r3, [fp, #-12]
40008444:	e2833001 	add	r3, r3, #1
40008448:	e50b300c 	str	r3, [fp, #-12]
4000844c:	e51b200c 	ldr	r2, [fp, #-12]
40008450:	e51b3020 	ldr	r3, [fp, #-32]	@ 0xffffffe0
40008454:	e1520003 	cmp	r2, r3
40008458:	3afffff3 	bcc	4000842c <memset+0x38>
4000845c:	e51b3010 	ldr	r3, [fp, #-16]
40008460:	e1a00003 	mov	r0, r3
40008464:	e28bd000 	add	sp, fp, #0
40008468:	e49db004 	pop	{fp}		@ (ldr fp, [sp], #4)
4000846c:	e12fff1e 	bx	lr

40008470 <memmove>:
40008470:	e52db004 	push	{fp}		@ (str fp, [sp, #-4]!)
40008474:	e28db000 	add	fp, sp, #0
40008478:	e24dd02c 	sub	sp, sp, #44	@ 0x2c
4000847c:	e50b0020 	str	r0, [fp, #-32]	@ 0xffffffe0
40008480:	e50b1024 	str	r1, [fp, #-36]	@ 0xffffffdc
40008484:	e50b2028 	str	r2, [fp, #-40]	@ 0xffffffd8
40008488:	e51b3020 	ldr	r3, [fp, #-32]	@ 0xffffffe0
4000848c:	e50b3018 	str	r3, [fp, #-24]	@ 0xffffffe8
40008490:	e51b3020 	ldr	r3, [fp, #-32]	@ 0xffffffe0
40008494:	e50b3008 	str	r3, [fp, #-8]
40008498:	e51b3024 	ldr	r3, [fp, #-36]	@ 0xffffffdc
4000849c:	e50b300c 	str	r3, [fp, #-12]
400084a0:	e51b2008 	ldr	r2, [fp, #-8]
400084a4:	e51b300c 	ldr	r3, [fp, #-12]
400084a8:	e1520003 	cmp	r2, r3
400084ac:	2a000012 	bcs	400084fc <memmove+0x8c>
400084b0:	e3a03000 	mov	r3, #0
400084b4:	e50b3010 	str	r3, [fp, #-16]
400084b8:	ea00000a 	b	400084e8 <memmove+0x78>
400084bc:	e51b200c 	ldr	r2, [fp, #-12]
400084c0:	e2823001 	add	r3, r2, #1
400084c4:	e50b300c 	str	r3, [fp, #-12]
400084c8:	e51b3008 	ldr	r3, [fp, #-8]
400084cc:	e2831001 	add	r1, r3, #1
400084d0:	e50b1008 	str	r1, [fp, #-8]
400084d4:	e5d22000 	ldrb	r2, [r2]
400084d8:	e5c32000 	strb	r2, [r3]
400084dc:	e51b3010 	ldr	r3, [fp, #-16]
400084e0:	e2833001 	add	r3, r3, #1
400084e4:	e50b3010 	str	r3, [fp, #-16]
400084e8:	e51b2010 	ldr	r2, [fp, #-16]
400084ec:	e51b3028 	ldr	r3, [fp, #-40]	@ 0xffffffd8
400084f0:	e1520003 	cmp	r2, r3
400084f4:	3afffff0 	bcc	400084bc <memmove+0x4c>
400084f8:	ea000022 	b	40008588 <memmove+0x118>
400084fc:	e51b2008 	ldr	r2, [fp, #-8]
40008500:	e51b300c 	ldr	r3, [fp, #-12]
40008504:	e1520003 	cmp	r2, r3
40008508:	9a00001c 	bls	40008580 <memmove+0x110>
4000850c:	e51b3028 	ldr	r3, [fp, #-40]	@ 0xffffffd8
40008510:	e2433001 	sub	r3, r3, #1
40008514:	e51b2008 	ldr	r2, [fp, #-8]
40008518:	e0823003 	add	r3, r2, r3
4000851c:	e50b3008 	str	r3, [fp, #-8]
40008520:	e51b3028 	ldr	r3, [fp, #-40]	@ 0xffffffd8
40008524:	e2433001 	sub	r3, r3, #1
40008528:	e51b200c 	ldr	r2, [fp, #-12]
4000852c:	e0823003 	add	r3, r2, r3
40008530:	e50b300c 	str	r3, [fp, #-12]
40008534:	e3a03000 	mov	r3, #0
40008538:	e50b3014 	str	r3, [fp, #-20]	@ 0xffffffec
4000853c:	ea00000a 	b	4000856c <memmove+0xfc>
40008540:	e51b200c 	ldr	r2, [fp, #-12]
40008544:	e2423001 	sub	r3, r2, #1
40008548:	e50b300c 	str	r3, [fp, #-12]
4000854c:	e51b3008 	ldr	r3, [fp, #-8]
40008550:	e2431001 	sub	r1, r3, #1
40008554:	e50b1008 	str	r1, [fp, #-8]
40008558:	e5d22000 	ldrb	r2, [r2]
4000855c:	e5c32000 	strb	r2, [r3]
40008560:	e51b3014 	ldr	r3, [fp, #-20]	@ 0xffffffec
40008564:	e2833001 	add	r3, r3, #1
40008568:	e50b3014 	str	r3, [fp, #-20]	@ 0xffffffec
4000856c:	e51b2014 	ldr	r2, [fp, #-20]	@ 0xffffffec
40008570:	e51b3028 	ldr	r3, [fp, #-40]	@ 0xffffffd8
40008574:	e1520003 	cmp	r2, r3
40008578:	3afffff0 	bcc	40008540 <memmove+0xd0>
4000857c:	ea000001 	b	40008588 <memmove+0x118>
40008580:	e51b3018 	ldr	r3, [fp, #-24]	@ 0xffffffe8
40008584:	ea000000 	b	4000858c <memmove+0x11c>
40008588:	e51b3018 	ldr	r3, [fp, #-24]	@ 0xffffffe8
4000858c:	e1a00003 	mov	r0, r3
40008590:	e28bd000 	add	sp, fp, #0
40008594:	e49db004 	pop	{fp}		@ (ldr fp, [sp], #4)
40008598:	e12fff1e 	bx	lr

4000859c <align_up>:
4000859c:	e52db004 	push	{fp}		@ (str fp, [sp, #-4]!)
400085a0:	e28db000 	add	fp, sp, #0
400085a4:	e24dd014 	sub	sp, sp, #20
400085a8:	e50b0010 	str	r0, [fp, #-16]
400085ac:	e50b1014 	str	r1, [fp, #-20]	@ 0xffffffec
400085b0:	e51b3014 	ldr	r3, [fp, #-20]	@ 0xffffffec
400085b4:	e3530000 	cmp	r3, #0
400085b8:	1a000001 	bne	400085c4 <align_up+0x28>
400085bc:	e51b3010 	ldr	r3, [fp, #-16]
400085c0:	ea000010 	b	40008608 <align_up+0x6c>
400085c4:	e51b3010 	ldr	r3, [fp, #-16]
400085c8:	e51b2014 	ldr	r2, [fp, #-20]	@ 0xffffffec
400085cc:	e732f213 	udiv	r2, r3, r2
400085d0:	e51b1014 	ldr	r1, [fp, #-20]	@ 0xffffffec
400085d4:	e0020291 	mul	r2, r1, r2
400085d8:	e0433002 	sub	r3, r3, r2
400085dc:	e50b3008 	str	r3, [fp, #-8]
400085e0:	e51b3008 	ldr	r3, [fp, #-8]
400085e4:	e3530000 	cmp	r3, #0
400085e8:	1a000001 	bne	400085f4 <align_up+0x58>
400085ec:	e51b3010 	ldr	r3, [fp, #-16]
400085f0:	ea000004 	b	40008608 <align_up+0x6c>
400085f4:	e51b2014 	ldr	r2, [fp, #-20]	@ 0xffffffec
400085f8:	e51b3008 	ldr	r3, [fp, #-8]
400085fc:	e0422003 	sub	r2, r2, r3
40008600:	e51b3010 	ldr	r3, [fp, #-16]
40008604:	e0823003 	add	r3, r2, r3
40008608:	e1a00003 	mov	r0, r3
4000860c:	e28bd000 	add	sp, fp, #0
40008610:	e49db004 	pop	{fp}		@ (ldr fp, [sp], #4)
40008614:	e12fff1e 	bx	lr

40008618 <is_digit>:
40008618:	e52db004 	push	{fp}		@ (str fp, [sp, #-4]!)
4000861c:	e28db000 	add	fp, sp, #0
40008620:	e24dd00c 	sub	sp, sp, #12
40008624:	e1a03000 	mov	r3, r0
40008628:	e54b3005 	strb	r3, [fp, #-5]
4000862c:	e55b3005 	ldrb	r3, [fp, #-5]
40008630:	e353002f 	cmp	r3, #47	@ 0x2f
40008634:	9a000004 	bls	4000864c <is_digit+0x34>
40008638:	e55b3005 	ldrb	r3, [fp, #-5]
4000863c:	e3530039 	cmp	r3, #57	@ 0x39
40008640:	8a000001 	bhi	4000864c <is_digit+0x34>
40008644:	e3a03001 	mov	r3, #1
40008648:	ea000000 	b	40008650 <is_digit+0x38>
4000864c:	e3a03000 	mov	r3, #0
40008650:	e1a00003 	mov	r0, r3
40008654:	e28bd000 	add	sp, fp, #0
40008658:	e49db004 	pop	{fp}		@ (ldr fp, [sp], #4)
4000865c:	e12fff1e 	bx	lr

40008660 <atoi>:
40008660:	e52db008 	str	fp, [sp, #-8]!
40008664:	e58de004 	str	lr, [sp, #4]
40008668:	e28db004 	add	fp, sp, #4
4000866c:	e24dd010 	sub	sp, sp, #16
40008670:	e50b0010 	str	r0, [fp, #-16]
40008674:	e3a03000 	mov	r3, #0
40008678:	e50b3008 	str	r3, [fp, #-8]
4000867c:	e3a03000 	mov	r3, #0
40008680:	e54b3009 	strb	r3, [fp, #-9]
40008684:	ea000002 	b	40008694 <atoi+0x34>
40008688:	e51b3010 	ldr	r3, [fp, #-16]
4000868c:	e2833001 	add	r3, r3, #1
40008690:	e50b3010 	str	r3, [fp, #-16]
40008694:	e51b3010 	ldr	r3, [fp, #-16]
40008698:	e5d33000 	ldrb	r3, [r3]
4000869c:	e3530020 	cmp	r3, #32
400086a0:	0afffff8 	beq	40008688 <atoi+0x28>
400086a4:	e51b3010 	ldr	r3, [fp, #-16]
400086a8:	e5d33000 	ldrb	r3, [r3]
400086ac:	e3530009 	cmp	r3, #9
400086b0:	0afffff4 	beq	40008688 <atoi+0x28>
400086b4:	e51b3010 	ldr	r3, [fp, #-16]
400086b8:	e5d33000 	ldrb	r3, [r3]
400086bc:	e353000a 	cmp	r3, #10
400086c0:	0afffff0 	beq	40008688 <atoi+0x28>
400086c4:	e51b3010 	ldr	r3, [fp, #-16]
400086c8:	e5d33000 	ldrb	r3, [r3]
400086cc:	e353002d 	cmp	r3, #45	@ 0x2d
400086d0:	1a000005 	bne	400086ec <atoi+0x8c>
400086d4:	e3a03001 	mov	r3, #1
400086d8:	e54b3009 	strb	r3, [fp, #-9]
400086dc:	e51b3010 	ldr	r3, [fp, #-16]
400086e0:	e2833001 	add	r3, r3, #1
400086e4:	e50b3010 	str	r3, [fp, #-16]
400086e8:	ea000016 	b	40008748 <atoi+0xe8>
400086ec:	e51b3010 	ldr	r3, [fp, #-16]
400086f0:	e5d33000 	ldrb	r3, [r3]
400086f4:	e353002b 	cmp	r3, #43	@ 0x2b
400086f8:	1a000012 	bne	40008748 <atoi+0xe8>
400086fc:	e3a03000 	mov	r3, #0
40008700:	e54b3009 	strb	r3, [fp, #-9]
40008704:	e51b3010 	ldr	r3, [fp, #-16]
40008708:	e2833001 	add	r3, r3, #1
4000870c:	e50b3010 	str	r3, [fp, #-16]
40008710:	ea00000c 	b	40008748 <atoi+0xe8>
40008714:	e51b2008 	ldr	r2, [fp, #-8]
40008718:	e1a03002 	mov	r3, r2
4000871c:	e1a03103 	lsl	r3, r3, #2
40008720:	e0833002 	add	r3, r3, r2
40008724:	e1a03083 	lsl	r3, r3, #1
40008728:	e1a01003 	mov	r1, r3
4000872c:	e51b3010 	ldr	r3, [fp, #-16]
40008730:	e2832001 	add	r2, r3, #1
40008734:	e50b2010 	str	r2, [fp, #-16]
40008738:	e5d33000 	ldrb	r3, [r3]
4000873c:	e2433030 	sub	r3, r3, #48	@ 0x30
40008740:	e0813003 	add	r3, r1, r3
40008744:	e50b3008 	str	r3, [fp, #-8]
40008748:	e51b3010 	ldr	r3, [fp, #-16]
4000874c:	e5d33000 	ldrb	r3, [r3]
40008750:	e1a00003 	mov	r0, r3
40008754:	ebffffaf 	bl	40008618 <is_digit>
40008758:	e1a03000 	mov	r3, r0
4000875c:	e3530000 	cmp	r3, #0
40008760:	1affffeb 	bne	40008714 <atoi+0xb4>
40008764:	e55b3009 	ldrb	r3, [fp, #-9]
40008768:	e3530000 	cmp	r3, #0
4000876c:	0a000002 	beq	4000877c <atoi+0x11c>
40008770:	e51b3008 	ldr	r3, [fp, #-8]
40008774:	e2633000 	rsb	r3, r3, #0
40008778:	ea000000 	b	40008780 <atoi+0x120>
4000877c:	e51b3008 	ldr	r3, [fp, #-8]
40008780:	e1a00003 	mov	r0, r3
40008784:	e24bd004 	sub	sp, fp, #4
40008788:	e59db000 	ldr	fp, [sp]
4000878c:	e28dd004 	add	sp, sp, #4
40008790:	e49df004 	pop	{pc}		@ (ldr pc, [sp], #4)

40008794 <itoa>:
40008794:	e52db004 	push	{fp}		@ (str fp, [sp, #-4]!)
40008798:	e28db000 	add	fp, sp, #0
4000879c:	e24dd044 	sub	sp, sp, #68	@ 0x44
400087a0:	e50b0038 	str	r0, [fp, #-56]	@ 0xffffffc8
400087a4:	e50b103c 	str	r1, [fp, #-60]	@ 0xffffffc4
400087a8:	e50b2040 	str	r2, [fp, #-64]	@ 0xffffffc0
400087ac:	e51b303c 	ldr	r3, [fp, #-60]	@ 0xffffffc4
400087b0:	e50b300c 	str	r3, [fp, #-12]
400087b4:	e51b3038 	ldr	r3, [fp, #-56]	@ 0xffffffc8
400087b8:	e3530000 	cmp	r3, #0
400087bc:	1a000009 	bne	400087e8 <itoa+0x54>
400087c0:	e51b303c 	ldr	r3, [fp, #-60]	@ 0xffffffc4
400087c4:	e2832001 	add	r2, r3, #1
400087c8:	e50b203c 	str	r2, [fp, #-60]	@ 0xffffffc4
400087cc:	e3a02030 	mov	r2, #48	@ 0x30
400087d0:	e5c32000 	strb	r2, [r3]
400087d4:	e51b303c 	ldr	r3, [fp, #-60]	@ 0xffffffc4
400087d8:	e3a02000 	mov	r2, #0
400087dc:	e5c32000 	strb	r2, [r3]
400087e0:	e51b300c 	ldr	r3, [fp, #-12]
400087e4:	ea000045 	b	40008900 <itoa+0x16c>
400087e8:	e51b3038 	ldr	r3, [fp, #-56]	@ 0xffffffc8
400087ec:	e3530000 	cmp	r3, #0
400087f0:	aa000008 	bge	40008818 <itoa+0x84>
400087f4:	e51b303c 	ldr	r3, [fp, #-60]	@ 0xffffffc4
400087f8:	e3a0202d 	mov	r2, #45	@ 0x2d
400087fc:	e5c32000 	strb	r2, [r3]
40008800:	e51b303c 	ldr	r3, [fp, #-60]	@ 0xffffffc4
40008804:	e2833001 	add	r3, r3, #1
40008808:	e50b303c 	str	r3, [fp, #-60]	@ 0xffffffc4
4000880c:	e51b3038 	ldr	r3, [fp, #-56]	@ 0xffffffc8
40008810:	e2633000 	rsb	r3, r3, #0
40008814:	e50b3038 	str	r3, [fp, #-56]	@ 0xffffffc8
40008818:	e24b3030 	sub	r3, fp, #48	@ 0x30
4000881c:	e50b3008 	str	r3, [fp, #-8]
40008820:	ea00001e 	b	400088a0 <itoa+0x10c>
40008824:	e51b3038 	ldr	r3, [fp, #-56]	@ 0xffffffc8
40008828:	e51b2040 	ldr	r2, [fp, #-64]	@ 0xffffffc0
4000882c:	e732f213 	udiv	r2, r3, r2
40008830:	e51b1040 	ldr	r1, [fp, #-64]	@ 0xffffffc0
40008834:	e0020291 	mul	r2, r1, r2
40008838:	e0433002 	sub	r3, r3, r2
4000883c:	e50b3010 	str	r3, [fp, #-16]
40008840:	e51b3010 	ldr	r3, [fp, #-16]
40008844:	e3530009 	cmp	r3, #9
40008848:	ca000008 	bgt	40008870 <itoa+0xdc>
4000884c:	e51b3010 	ldr	r3, [fp, #-16]
40008850:	e6ef2073 	uxtb	r2, r3
40008854:	e51b3008 	ldr	r3, [fp, #-8]
40008858:	e2831001 	add	r1, r3, #1
4000885c:	e50b1008 	str	r1, [fp, #-8]
40008860:	e2822030 	add	r2, r2, #48	@ 0x30
40008864:	e6ef2072 	uxtb	r2, r2
40008868:	e5c32000 	strb	r2, [r3]
4000886c:	ea000007 	b	40008890 <itoa+0xfc>
40008870:	e51b3010 	ldr	r3, [fp, #-16]
40008874:	e6ef2073 	uxtb	r2, r3
40008878:	e51b3008 	ldr	r3, [fp, #-8]
4000887c:	e2831001 	add	r1, r3, #1
40008880:	e50b1008 	str	r1, [fp, #-8]
40008884:	e2822057 	add	r2, r2, #87	@ 0x57
40008888:	e6ef2072 	uxtb	r2, r2
4000888c:	e5c32000 	strb	r2, [r3]
40008890:	e51b2038 	ldr	r2, [fp, #-56]	@ 0xffffffc8
40008894:	e51b3040 	ldr	r3, [fp, #-64]	@ 0xffffffc0
40008898:	e733f312 	udiv	r3, r2, r3
4000889c:	e50b3038 	str	r3, [fp, #-56]	@ 0xffffffc8
400088a0:	e51b3038 	ldr	r3, [fp, #-56]	@ 0xffffffc8
400088a4:	e3530000 	cmp	r3, #0
400088a8:	1affffdd 	bne	40008824 <itoa+0x90>
400088ac:	e51b3008 	ldr	r3, [fp, #-8]
400088b0:	e2433001 	sub	r3, r3, #1
400088b4:	e50b3008 	str	r3, [fp, #-8]
400088b8:	ea000008 	b	400088e0 <itoa+0x14c>
400088bc:	e51b303c 	ldr	r3, [fp, #-60]	@ 0xffffffc4
400088c0:	e2832001 	add	r2, r3, #1
400088c4:	e50b203c 	str	r2, [fp, #-60]	@ 0xffffffc4
400088c8:	e51b2008 	ldr	r2, [fp, #-8]
400088cc:	e5d22000 	ldrb	r2, [r2]
400088d0:	e5c32000 	strb	r2, [r3]
400088d4:	e51b3008 	ldr	r3, [fp, #-8]
400088d8:	e2433001 	sub	r3, r3, #1
400088dc:	e50b3008 	str	r3, [fp, #-8]
400088e0:	e24b3030 	sub	r3, fp, #48	@ 0x30
400088e4:	e51b2008 	ldr	r2, [fp, #-8]
400088e8:	e1520003 	cmp	r2, r3
400088ec:	2afffff2 	bcs	400088bc <itoa+0x128>
400088f0:	e51b303c 	ldr	r3, [fp, #-60]	@ 0xffffffc4
400088f4:	e3a02000 	mov	r2, #0
400088f8:	e5c32000 	strb	r2, [r3]
400088fc:	e51b300c 	ldr	r3, [fp, #-12]
40008900:	e1a00003 	mov	r0, r3
40008904:	e28bd000 	add	sp, fp, #0
40008908:	e49db004 	pop	{fp}		@ (ldr fp, [sp], #4)
4000890c:	e12fff1e 	bx	lr

40008910 <utoa>:
40008910:	e52db004 	push	{fp}		@ (str fp, [sp, #-4]!)
40008914:	e28db000 	add	fp, sp, #0
40008918:	e24dd044 	sub	sp, sp, #68	@ 0x44
4000891c:	e50b0038 	str	r0, [fp, #-56]	@ 0xffffffc8
40008920:	e50b103c 	str	r1, [fp, #-60]	@ 0xffffffc4
40008924:	e50b2040 	str	r2, [fp, #-64]	@ 0xffffffc0
40008928:	e51b303c 	ldr	r3, [fp, #-60]	@ 0xffffffc4
4000892c:	e50b300c 	str	r3, [fp, #-12]
40008930:	e51b3038 	ldr	r3, [fp, #-56]	@ 0xffffffc8
40008934:	e3530000 	cmp	r3, #0
40008938:	1a000009 	bne	40008964 <utoa+0x54>
4000893c:	e51b303c 	ldr	r3, [fp, #-60]	@ 0xffffffc4
40008940:	e2832001 	add	r2, r3, #1
40008944:	e50b203c 	str	r2, [fp, #-60]	@ 0xffffffc4
40008948:	e3a02030 	mov	r2, #48	@ 0x30
4000894c:	e5c32000 	strb	r2, [r3]
40008950:	e51b303c 	ldr	r3, [fp, #-60]	@ 0xffffffc4
40008954:	e3a02000 	mov	r2, #0
40008958:	e5c32000 	strb	r2, [r3]
4000895c:	e51b300c 	ldr	r3, [fp, #-12]
40008960:	ea000039 	b	40008a4c <utoa+0x13c>
40008964:	e24b3030 	sub	r3, fp, #48	@ 0x30
40008968:	e50b3008 	str	r3, [fp, #-8]
4000896c:	ea00001e 	b	400089ec <utoa+0xdc>
40008970:	e51b3038 	ldr	r3, [fp, #-56]	@ 0xffffffc8
40008974:	e51b2040 	ldr	r2, [fp, #-64]	@ 0xffffffc0
40008978:	e732f213 	udiv	r2, r3, r2
4000897c:	e51b1040 	ldr	r1, [fp, #-64]	@ 0xffffffc0
40008980:	e0020291 	mul	r2, r1, r2
40008984:	e0433002 	sub	r3, r3, r2
40008988:	e50b3010 	str	r3, [fp, #-16]
4000898c:	e51b3010 	ldr	r3, [fp, #-16]
40008990:	e3530009 	cmp	r3, #9
40008994:	ca000008 	bgt	400089bc <utoa+0xac>
40008998:	e51b3010 	ldr	r3, [fp, #-16]
4000899c:	e6ef2073 	uxtb	r2, r3
400089a0:	e51b3008 	ldr	r3, [fp, #-8]
400089a4:	e2831001 	add	r1, r3, #1
400089a8:	e50b1008 	str	r1, [fp, #-8]
400089ac:	e2822030 	add	r2, r2, #48	@ 0x30
400089b0:	e6ef2072 	uxtb	r2, r2
400089b4:	e5c32000 	strb	r2, [r3]
400089b8:	ea000007 	b	400089dc <utoa+0xcc>
400089bc:	e51b3010 	ldr	r3, [fp, #-16]
400089c0:	e6ef2073 	uxtb	r2, r3
400089c4:	e51b3008 	ldr	r3, [fp, #-8]
400089c8:	e2831001 	add	r1, r3, #1
400089cc:	e50b1008 	str	r1, [fp, #-8]
400089d0:	e2822057 	add	r2, r2, #87	@ 0x57
400089d4:	e6ef2072 	uxtb	r2, r2
400089d8:	e5c32000 	strb	r2, [r3]
400089dc:	e51b2038 	ldr	r2, [fp, #-56]	@ 0xffffffc8
400089e0:	e51b3040 	ldr	r3, [fp, #-64]	@ 0xffffffc0
400089e4:	e733f312 	udiv	r3, r2, r3
400089e8:	e50b3038 	str	r3, [fp, #-56]	@ 0xffffffc8
400089ec:	e51b3038 	ldr	r3, [fp, #-56]	@ 0xffffffc8
400089f0:	e3530000 	cmp	r3, #0
400089f4:	1affffdd 	bne	40008970 <utoa+0x60>
400089f8:	e51b3008 	ldr	r3, [fp, #-8]
400089fc:	e2433001 	sub	r3, r3, #1
40008a00:	e50b3008 	str	r3, [fp, #-8]
40008a04:	ea000008 	b	40008a2c <utoa+0x11c>
40008a08:	e51b303c 	ldr	r3, [fp, #-60]	@ 0xffffffc4
40008a0c:	e2832001 	add	r2, r3, #1
40008a10:	e50b203c 	str	r2, [fp, #-60]	@ 0xffffffc4
40008a14:	e51b2008 	ldr	r2, [fp, #-8]
40008a18:	e5d22000 	ldrb	r2, [r2]
40008a1c:	e5c32000 	strb	r2, [r3]
40008a20:	e51b3008 	ldr	r3, [fp, #-8]
40008a24:	e2433001 	sub	r3, r3, #1
40008a28:	e50b3008 	str	r3, [fp, #-8]
40008a2c:	e24b3030 	sub	r3, fp, #48	@ 0x30
40008a30:	e51b2008 	ldr	r2, [fp, #-8]
40008a34:	e1520003 	cmp	r2, r3
40008a38:	2afffff2 	bcs	40008a08 <utoa+0xf8>
40008a3c:	e51b303c 	ldr	r3, [fp, #-60]	@ 0xffffffc4
40008a40:	e3a02000 	mov	r2, #0
40008a44:	e5c32000 	strb	r2, [r3]
40008a48:	e51b300c 	ldr	r3, [fp, #-12]
40008a4c:	e1a00003 	mov	r0, r3
40008a50:	e28bd000 	add	sp, fp, #0
40008a54:	e49db004 	pop	{fp}		@ (ldr fp, [sp], #4)
40008a58:	e12fff1e 	bx	lr

40008a5c <strlen>:
40008a5c:	e52db004 	push	{fp}		@ (str fp, [sp, #-4]!)
40008a60:	e28db000 	add	fp, sp, #0
40008a64:	e24dd014 	sub	sp, sp, #20
40008a68:	e50b0010 	str	r0, [fp, #-16]
40008a6c:	e3a03000 	mov	r3, #0
40008a70:	e50b3008 	str	r3, [fp, #-8]
40008a74:	ea000002 	b	40008a84 <strlen+0x28>
40008a78:	e51b3008 	ldr	r3, [fp, #-8]
40008a7c:	e2833001 	add	r3, r3, #1
40008a80:	e50b3008 	str	r3, [fp, #-8]
40008a84:	e51b3010 	ldr	r3, [fp, #-16]
40008a88:	e2832001 	add	r2, r3, #1
40008a8c:	e50b2010 	str	r2, [fp, #-16]
40008a90:	e5d33000 	ldrb	r3, [r3]
40008a94:	e3530000 	cmp	r3, #0
40008a98:	1afffff6 	bne	40008a78 <strlen+0x1c>
40008a9c:	e51b3008 	ldr	r3, [fp, #-8]
40008aa0:	e1a00003 	mov	r0, r3
40008aa4:	e28bd000 	add	sp, fp, #0
40008aa8:	e49db004 	pop	{fp}		@ (ldr fp, [sp], #4)
40008aac:	e12fff1e 	bx	lr

40008ab0 <strcat>:
40008ab0:	e52db008 	str	fp, [sp, #-8]!
40008ab4:	e58de004 	str	lr, [sp, #4]
40008ab8:	e28db004 	add	fp, sp, #4
40008abc:	e24dd010 	sub	sp, sp, #16
40008ac0:	e50b0010 	str	r0, [fp, #-16]
40008ac4:	e50b1014 	str	r1, [fp, #-20]	@ 0xffffffec
40008ac8:	e51b3010 	ldr	r3, [fp, #-16]
40008acc:	e50b3008 	str	r3, [fp, #-8]
40008ad0:	ea000002 	b	40008ae0 <strcat+0x30>
40008ad4:	e51b3008 	ldr	r3, [fp, #-8]
40008ad8:	e2833001 	add	r3, r3, #1
40008adc:	e50b3008 	str	r3, [fp, #-8]
40008ae0:	e51b3008 	ldr	r3, [fp, #-8]
40008ae4:	e5d33000 	ldrb	r3, [r3]
40008ae8:	e3530000 	cmp	r3, #0
40008aec:	1afffff8 	bne	40008ad4 <strcat+0x24>
40008af0:	e51b1014 	ldr	r1, [fp, #-20]	@ 0xffffffec
40008af4:	e51b0008 	ldr	r0, [fp, #-8]
40008af8:	eb000083 	bl	40008d0c <strcpy>
40008afc:	e51b3008 	ldr	r3, [fp, #-8]
40008b00:	e3a02000 	mov	r2, #0
40008b04:	e5c32000 	strb	r2, [r3]
40008b08:	e51b3010 	ldr	r3, [fp, #-16]
40008b0c:	e1a00003 	mov	r0, r3
40008b10:	e24bd004 	sub	sp, fp, #4
40008b14:	e59db000 	ldr	fp, [sp]
40008b18:	e28dd004 	add	sp, sp, #4
40008b1c:	e49df004 	pop	{pc}		@ (ldr pc, [sp], #4)

40008b20 <strncat>:
40008b20:	e52db008 	str	fp, [sp, #-8]!
40008b24:	e58de004 	str	lr, [sp, #4]
40008b28:	e28db004 	add	fp, sp, #4
40008b2c:	e24dd018 	sub	sp, sp, #24
40008b30:	e50b0010 	str	r0, [fp, #-16]
40008b34:	e50b1014 	str	r1, [fp, #-20]	@ 0xffffffec
40008b38:	e50b2018 	str	r2, [fp, #-24]	@ 0xffffffe8
40008b3c:	e51b3010 	ldr	r3, [fp, #-16]
40008b40:	e50b3008 	str	r3, [fp, #-8]
40008b44:	ea000002 	b	40008b54 <strncat+0x34>
40008b48:	e51b3008 	ldr	r3, [fp, #-8]
40008b4c:	e2833001 	add	r3, r3, #1
40008b50:	e50b3008 	str	r3, [fp, #-8]
40008b54:	e51b3008 	ldr	r3, [fp, #-8]
40008b58:	e5d33000 	ldrb	r3, [r3]
40008b5c:	e3530000 	cmp	r3, #0
40008b60:	1afffff8 	bne	40008b48 <strncat+0x28>
40008b64:	e51b2018 	ldr	r2, [fp, #-24]	@ 0xffffffe8
40008b68:	e51b1014 	ldr	r1, [fp, #-20]	@ 0xffffffec
40008b6c:	e51b0008 	ldr	r0, [fp, #-8]
40008b70:	eb00007d 	bl	40008d6c <strncpy>
40008b74:	e51b3010 	ldr	r3, [fp, #-16]
40008b78:	e1a00003 	mov	r0, r3
40008b7c:	e24bd004 	sub	sp, fp, #4
40008b80:	e59db000 	ldr	fp, [sp]
40008b84:	e28dd004 	add	sp, sp, #4
40008b88:	e49df004 	pop	{pc}		@ (ldr pc, [sp], #4)

40008b8c <strcmp>:
40008b8c:	e52db004 	push	{fp}		@ (str fp, [sp, #-4]!)
40008b90:	e28db000 	add	fp, sp, #0
40008b94:	e24dd014 	sub	sp, sp, #20
40008b98:	e50b0010 	str	r0, [fp, #-16]
40008b9c:	e50b1014 	str	r1, [fp, #-20]	@ 0xffffffec
40008ba0:	ea00000f 	b	40008be4 <strcmp+0x58>
40008ba4:	e51b3010 	ldr	r3, [fp, #-16]
40008ba8:	e2832001 	add	r2, r3, #1
40008bac:	e50b2010 	str	r2, [fp, #-16]
40008bb0:	e5d33000 	ldrb	r3, [r3]
40008bb4:	e1a01003 	mov	r1, r3
40008bb8:	e51b3014 	ldr	r3, [fp, #-20]	@ 0xffffffec
40008bbc:	e2832001 	add	r2, r3, #1
40008bc0:	e50b2014 	str	r2, [fp, #-20]	@ 0xffffffec
40008bc4:	e5d33000 	ldrb	r3, [r3]
40008bc8:	e0413003 	sub	r3, r1, r3
40008bcc:	e50b3008 	str	r3, [fp, #-8]
40008bd0:	e51b3008 	ldr	r3, [fp, #-8]
40008bd4:	e3530000 	cmp	r3, #0
40008bd8:	0a000001 	beq	40008be4 <strcmp+0x58>
40008bdc:	e51b3008 	ldr	r3, [fp, #-8]
40008be0:	ea00000d 	b	40008c1c <strcmp+0x90>
40008be4:	e51b3010 	ldr	r3, [fp, #-16]
40008be8:	e5d33000 	ldrb	r3, [r3]
40008bec:	e3530000 	cmp	r3, #0
40008bf0:	0a000003 	beq	40008c04 <strcmp+0x78>
40008bf4:	e51b3014 	ldr	r3, [fp, #-20]	@ 0xffffffec
40008bf8:	e5d33000 	ldrb	r3, [r3]
40008bfc:	e3530000 	cmp	r3, #0
40008c00:	1affffe7 	bne	40008ba4 <strcmp+0x18>
40008c04:	e51b3010 	ldr	r3, [fp, #-16]
40008c08:	e5d33000 	ldrb	r3, [r3]
40008c0c:	e1a02003 	mov	r2, r3
40008c10:	e51b3014 	ldr	r3, [fp, #-20]	@ 0xffffffec
40008c14:	e5d33000 	ldrb	r3, [r3]
40008c18:	e0423003 	sub	r3, r2, r3
40008c1c:	e1a00003 	mov	r0, r3
40008c20:	e28bd000 	add	sp, fp, #0
40008c24:	e49db004 	pop	{fp}		@ (ldr fp, [sp], #4)
40008c28:	e12fff1e 	bx	lr

40008c2c <strncmp>:
40008c2c:	e52db004 	push	{fp}		@ (str fp, [sp, #-4]!)
40008c30:	e28db000 	add	fp, sp, #0
40008c34:	e24dd01c 	sub	sp, sp, #28
40008c38:	e50b0010 	str	r0, [fp, #-16]
40008c3c:	e50b1014 	str	r1, [fp, #-20]	@ 0xffffffec
40008c40:	e50b2018 	str	r2, [fp, #-24]	@ 0xffffffe8
40008c44:	e3a03000 	mov	r3, #0
40008c48:	e50b3008 	str	r3, [fp, #-8]
40008c4c:	ea000012 	b	40008c9c <strncmp+0x70>
40008c50:	e51b3010 	ldr	r3, [fp, #-16]
40008c54:	e2832001 	add	r2, r3, #1
40008c58:	e50b2010 	str	r2, [fp, #-16]
40008c5c:	e5d32000 	ldrb	r2, [r3]
40008c60:	e51b3014 	ldr	r3, [fp, #-20]	@ 0xffffffec
40008c64:	e2831001 	add	r1, r3, #1
40008c68:	e50b1014 	str	r1, [fp, #-20]	@ 0xffffffec
40008c6c:	e5d33000 	ldrb	r3, [r3]
40008c70:	e0423003 	sub	r3, r2, r3
40008c74:	e6ef3073 	uxtb	r3, r3
40008c78:	e54b3009 	strb	r3, [fp, #-9]
40008c7c:	e15b30d9 	ldrsb	r3, [fp, #-9]
40008c80:	e3530000 	cmp	r3, #0
40008c84:	0a000001 	beq	40008c90 <strncmp+0x64>
40008c88:	e15b30d9 	ldrsb	r3, [fp, #-9]
40008c8c:	ea00001a 	b	40008cfc <strncmp+0xd0>
40008c90:	e51b3008 	ldr	r3, [fp, #-8]
40008c94:	e2833001 	add	r3, r3, #1
40008c98:	e50b3008 	str	r3, [fp, #-8]
40008c9c:	e51b3010 	ldr	r3, [fp, #-16]
40008ca0:	e5d33000 	ldrb	r3, [r3]
40008ca4:	e3530000 	cmp	r3, #0
40008ca8:	0a000007 	beq	40008ccc <strncmp+0xa0>
40008cac:	e51b3014 	ldr	r3, [fp, #-20]	@ 0xffffffec
40008cb0:	e5d33000 	ldrb	r3, [r3]
40008cb4:	e3530000 	cmp	r3, #0
40008cb8:	0a000003 	beq	40008ccc <strncmp+0xa0>
40008cbc:	e51b2008 	ldr	r2, [fp, #-8]
40008cc0:	e51b3018 	ldr	r3, [fp, #-24]	@ 0xffffffe8
40008cc4:	e1520003 	cmp	r2, r3
40008cc8:	3affffe0 	bcc	40008c50 <strncmp+0x24>
40008ccc:	e51b2008 	ldr	r2, [fp, #-8]
40008cd0:	e51b3018 	ldr	r3, [fp, #-24]	@ 0xffffffe8
40008cd4:	e1520003 	cmp	r2, r3
40008cd8:	1a000001 	bne	40008ce4 <strncmp+0xb8>
40008cdc:	e3a03000 	mov	r3, #0
40008ce0:	ea000005 	b	40008cfc <strncmp+0xd0>
40008ce4:	e51b3010 	ldr	r3, [fp, #-16]
40008ce8:	e5d33000 	ldrb	r3, [r3]
40008cec:	e1a02003 	mov	r2, r3
40008cf0:	e51b3014 	ldr	r3, [fp, #-20]	@ 0xffffffec
40008cf4:	e5d33000 	ldrb	r3, [r3]
40008cf8:	e0423003 	sub	r3, r2, r3
40008cfc:	e1a00003 	mov	r0, r3
40008d00:	e28bd000 	add	sp, fp, #0
40008d04:	e49db004 	pop	{fp}		@ (ldr fp, [sp], #4)
40008d08:	e12fff1e 	bx	lr

40008d0c <strcpy>:
40008d0c:	e52db004 	push	{fp}		@ (str fp, [sp, #-4]!)
40008d10:	e28db000 	add	fp, sp, #0
40008d14:	e24dd014 	sub	sp, sp, #20
40008d18:	e50b0010 	str	r0, [fp, #-16]
40008d1c:	e50b1014 	str	r1, [fp, #-20]	@ 0xffffffec
40008d20:	e51b3010 	ldr	r3, [fp, #-16]
40008d24:	e50b3008 	str	r3, [fp, #-8]
40008d28:	e320f000 	nop	{0}
40008d2c:	e51b2014 	ldr	r2, [fp, #-20]	@ 0xffffffec
40008d30:	e2823001 	add	r3, r2, #1
40008d34:	e50b3014 	str	r3, [fp, #-20]	@ 0xffffffec
40008d38:	e51b3010 	ldr	r3, [fp, #-16]
40008d3c:	e2831001 	add	r1, r3, #1
40008d40:	e50b1010 	str	r1, [fp, #-16]
40008d44:	e5d22000 	ldrb	r2, [r2]
40008d48:	e5c32000 	strb	r2, [r3]
40008d4c:	e5d33000 	ldrb	r3, [r3]
40008d50:	e3530000 	cmp	r3, #0
40008d54:	1afffff4 	bne	40008d2c <strcpy+0x20>
40008d58:	e51b3008 	ldr	r3, [fp, #-8]
40008d5c:	e1a00003 	mov	r0, r3
40008d60:	e28bd000 	add	sp, fp, #0
40008d64:	e49db004 	pop	{fp}		@ (ldr fp, [sp], #4)
40008d68:	e12fff1e 	bx	lr

40008d6c <strncpy>:
40008d6c:	e52db004 	push	{fp}		@ (str fp, [sp, #-4]!)
40008d70:	e28db000 	add	fp, sp, #0
40008d74:	e24dd01c 	sub	sp, sp, #28
40008d78:	e50b0010 	str	r0, [fp, #-16]
40008d7c:	e50b1014 	str	r1, [fp, #-20]	@ 0xffffffec
40008d80:	e50b2018 	str	r2, [fp, #-24]	@ 0xffffffe8
40008d84:	e51b3010 	ldr	r3, [fp, #-16]
40008d88:	e50b3008 	str	r3, [fp, #-8]
40008d8c:	ea000002 	b	40008d9c <strncpy+0x30>
40008d90:	e51b3018 	ldr	r3, [fp, #-24]	@ 0xffffffe8
40008d94:	e2433001 	sub	r3, r3, #1
40008d98:	e50b3018 	str	r3, [fp, #-24]	@ 0xffffffe8
40008d9c:	e51b3018 	ldr	r3, [fp, #-24]	@ 0xffffffe8
40008da0:	e3530000 	cmp	r3, #0
40008da4:	0a000010 	beq	40008dec <strncpy+0x80>
40008da8:	e51b2014 	ldr	r2, [fp, #-20]	@ 0xffffffec
40008dac:	e2823001 	add	r3, r2, #1
40008db0:	e50b3014 	str	r3, [fp, #-20]	@ 0xffffffec
40008db4:	e51b3010 	ldr	r3, [fp, #-16]
40008db8:	e2831001 	add	r1, r3, #1
40008dbc:	e50b1010 	str	r1, [fp, #-16]
40008dc0:	e5d22000 	ldrb	r2, [r2]
40008dc4:	e5c32000 	strb	r2, [r3]
40008dc8:	e5d33000 	ldrb	r3, [r3]
40008dcc:	e3530000 	cmp	r3, #0
40008dd0:	1affffee 	bne	40008d90 <strncpy+0x24>
40008dd4:	ea000004 	b	40008dec <strncpy+0x80>
40008dd8:	e51b3010 	ldr	r3, [fp, #-16]
40008ddc:	e2832001 	add	r2, r3, #1
40008de0:	e50b2010 	str	r2, [fp, #-16]
40008de4:	e3a02000 	mov	r2, #0
40008de8:	e5c32000 	strb	r2, [r3]
40008dec:	e51b3018 	ldr	r3, [fp, #-24]	@ 0xffffffe8
40008df0:	e2432001 	sub	r2, r3, #1
40008df4:	e50b2018 	str	r2, [fp, #-24]	@ 0xffffffe8
40008df8:	e3530000 	cmp	r3, #0
40008dfc:	1afffff5 	bne	40008dd8 <strncpy+0x6c>
40008e00:	e51b3008 	ldr	r3, [fp, #-8]
40008e04:	e1a00003 	mov	r0, r3
40008e08:	e28bd000 	add	sp, fp, #0
40008e0c:	e49db004 	pop	{fp}		@ (ldr fp, [sp], #4)
40008e10:	e12fff1e 	bx	lr

40008e14 <strchr>:
40008e14:	e52db004 	push	{fp}		@ (str fp, [sp, #-4]!)
40008e18:	e28db000 	add	fp, sp, #0
40008e1c:	e24dd00c 	sub	sp, sp, #12
40008e20:	e50b0008 	str	r0, [fp, #-8]
40008e24:	e50b100c 	str	r1, [fp, #-12]
40008e28:	ea00000a 	b	40008e58 <strchr+0x44>
40008e2c:	e51b3008 	ldr	r3, [fp, #-8]
40008e30:	e5d32000 	ldrb	r2, [r3]
40008e34:	e51b300c 	ldr	r3, [fp, #-12]
40008e38:	e6ef3073 	uxtb	r3, r3
40008e3c:	e1520003 	cmp	r2, r3
40008e40:	1a000001 	bne	40008e4c <strchr+0x38>
40008e44:	e51b3008 	ldr	r3, [fp, #-8]
40008e48:	ea000007 	b	40008e6c <strchr+0x58>
40008e4c:	e51b3008 	ldr	r3, [fp, #-8]
40008e50:	e2833001 	add	r3, r3, #1
40008e54:	e50b3008 	str	r3, [fp, #-8]
40008e58:	e51b3008 	ldr	r3, [fp, #-8]
40008e5c:	e5d33000 	ldrb	r3, [r3]
40008e60:	e3530000 	cmp	r3, #0
40008e64:	1afffff0 	bne	40008e2c <strchr+0x18>
40008e68:	e3a03000 	mov	r3, #0
40008e6c:	e1a00003 	mov	r0, r3
40008e70:	e28bd000 	add	sp, fp, #0
40008e74:	e49db004 	pop	{fp}		@ (ldr fp, [sp], #4)
40008e78:	e12fff1e 	bx	lr

40008e7c <strfmt>:
40008e7c:	e92d000e 	push	{r1, r2, r3}
40008e80:	e52db008 	str	fp, [sp, #-8]!
40008e84:	e58de004 	str	lr, [sp, #4]
40008e88:	e28db004 	add	fp, sp, #4
40008e8c:	e24dd014 	sub	sp, sp, #20
40008e90:	e50b0014 	str	r0, [fp, #-20]	@ 0xffffffec
40008e94:	e28b3008 	add	r3, fp, #8
40008e98:	e50b300c 	str	r3, [fp, #-12]
40008e9c:	e51b200c 	ldr	r2, [fp, #-12]
40008ea0:	e59b1004 	ldr	r1, [fp, #4]
40008ea4:	e51b0014 	ldr	r0, [fp, #-20]	@ 0xffffffec
40008ea8:	eb000006 	bl	40008ec8 <vstrfmt>
40008eac:	e320f000 	nop	{0}
40008eb0:	e24bd004 	sub	sp, fp, #4
40008eb4:	e59db000 	ldr	fp, [sp]
40008eb8:	e59de004 	ldr	lr, [sp, #4]
40008ebc:	e28dd008 	add	sp, sp, #8
40008ec0:	e28dd00c 	add	sp, sp, #12
40008ec4:	e12fff1e 	bx	lr

40008ec8 <vstrfmt>:
40008ec8:	e52db008 	str	fp, [sp, #-8]!
40008ecc:	e58de004 	str	lr, [sp, #4]
40008ed0:	e28db004 	add	fp, sp, #4
40008ed4:	e24dd068 	sub	sp, sp, #104	@ 0x68
40008ed8:	e50b0060 	str	r0, [fp, #-96]	@ 0xffffffa0
40008edc:	e50b1064 	str	r1, [fp, #-100]	@ 0xffffff9c
40008ee0:	e50b2068 	str	r2, [fp, #-104]	@ 0xffffff98
40008ee4:	ea0000f5 	b	400092c0 <vstrfmt+0x3f8>
40008ee8:	e51b3064 	ldr	r3, [fp, #-100]	@ 0xffffff9c
40008eec:	e5d33000 	ldrb	r3, [r3]
40008ef0:	e3530025 	cmp	r3, #37	@ 0x25
40008ef4:	1a0000ea 	bne	400092a4 <vstrfmt+0x3dc>
40008ef8:	e51b3064 	ldr	r3, [fp, #-100]	@ 0xffffff9c
40008efc:	e2833001 	add	r3, r3, #1
40008f00:	e50b3064 	str	r3, [fp, #-100]	@ 0xffffff9c
40008f04:	e51b3064 	ldr	r3, [fp, #-100]	@ 0xffffff9c
40008f08:	e5d33000 	ldrb	r3, [r3]
40008f0c:	e3530078 	cmp	r3, #120	@ 0x78
40008f10:	0a00008b 	beq	40009144 <vstrfmt+0x27c>
40008f14:	e3530078 	cmp	r3, #120	@ 0x78
40008f18:	ca0000d8 	bgt	40009280 <vstrfmt+0x3b8>
40008f1c:	e3530075 	cmp	r3, #117	@ 0x75
40008f20:	0a00004f 	beq	40009064 <vstrfmt+0x19c>
40008f24:	e3530075 	cmp	r3, #117	@ 0x75
40008f28:	ca0000d4 	bgt	40009280 <vstrfmt+0x3b8>
40008f2c:	e3530073 	cmp	r3, #115	@ 0x73
40008f30:	0a000020 	beq	40008fb8 <vstrfmt+0xf0>
40008f34:	e3530073 	cmp	r3, #115	@ 0x73
40008f38:	ca0000d0 	bgt	40009280 <vstrfmt+0x3b8>
40008f3c:	e3530070 	cmp	r3, #112	@ 0x70
40008f40:	0a000060 	beq	400090c8 <vstrfmt+0x200>
40008f44:	e3530070 	cmp	r3, #112	@ 0x70
40008f48:	ca0000cc 	bgt	40009280 <vstrfmt+0x3b8>
40008f4c:	e353006f 	cmp	r3, #111	@ 0x6f
40008f50:	0a000094 	beq	400091a8 <vstrfmt+0x2e0>
40008f54:	e353006f 	cmp	r3, #111	@ 0x6f
40008f58:	ca0000c8 	bgt	40009280 <vstrfmt+0x3b8>
40008f5c:	e3530064 	cmp	r3, #100	@ 0x64
40008f60:	0a000026 	beq	40009000 <vstrfmt+0x138>
40008f64:	e3530064 	cmp	r3, #100	@ 0x64
40008f68:	ca0000c4 	bgt	40009280 <vstrfmt+0x3b8>
40008f6c:	e3530063 	cmp	r3, #99	@ 0x63
40008f70:	0a000006 	beq	40008f90 <vstrfmt+0xc8>
40008f74:	e3530063 	cmp	r3, #99	@ 0x63
40008f78:	ca0000c0 	bgt	40009280 <vstrfmt+0x3b8>
40008f7c:	e3530025 	cmp	r3, #37	@ 0x25
40008f80:	0a0000ba 	beq	40009270 <vstrfmt+0x3a8>
40008f84:	e3530062 	cmp	r3, #98	@ 0x62
40008f88:	0a00009f 	beq	4000920c <vstrfmt+0x344>
40008f8c:	ea0000bb 	b	40009280 <vstrfmt+0x3b8>
40008f90:	e51b3068 	ldr	r3, [fp, #-104]	@ 0xffffff98
40008f94:	e2832004 	add	r2, r3, #4
40008f98:	e50b2068 	str	r2, [fp, #-104]	@ 0xffffff98
40008f9c:	e5933000 	ldr	r3, [r3]
40008fa0:	e54b3035 	strb	r3, [fp, #-53]	@ 0xffffffcb
40008fa4:	e55b2035 	ldrb	r2, [fp, #-53]	@ 0xffffffcb
40008fa8:	e51b3060 	ldr	r3, [fp, #-96]	@ 0xffffffa0
40008fac:	e1a00002 	mov	r0, r2
40008fb0:	e12fff33 	blx	r3
40008fb4:	ea0000b6 	b	40009294 <vstrfmt+0x3cc>
40008fb8:	e51b3068 	ldr	r3, [fp, #-104]	@ 0xffffff98
40008fbc:	e2832004 	add	r2, r3, #4
40008fc0:	e50b2068 	str	r2, [fp, #-104]	@ 0xffffff98
40008fc4:	e5933000 	ldr	r3, [r3]
40008fc8:	e50b3008 	str	r3, [fp, #-8]
40008fcc:	ea000006 	b	40008fec <vstrfmt+0x124>
40008fd0:	e51b3008 	ldr	r3, [fp, #-8]
40008fd4:	e2832001 	add	r2, r3, #1
40008fd8:	e50b2008 	str	r2, [fp, #-8]
40008fdc:	e5d32000 	ldrb	r2, [r3]
40008fe0:	e51b3060 	ldr	r3, [fp, #-96]	@ 0xffffffa0
40008fe4:	e1a00002 	mov	r0, r2
40008fe8:	e12fff33 	blx	r3
40008fec:	e51b3008 	ldr	r3, [fp, #-8]
40008ff0:	e5d33000 	ldrb	r3, [r3]
40008ff4:	e3530000 	cmp	r3, #0
40008ff8:	1afffff4 	bne	40008fd0 <vstrfmt+0x108>
40008ffc:	ea0000a4 	b	40009294 <vstrfmt+0x3cc>
40009000:	e51b3068 	ldr	r3, [fp, #-104]	@ 0xffffff98
40009004:	e2832004 	add	r2, r3, #4
40009008:	e50b2068 	str	r2, [fp, #-104]	@ 0xffffff98
4000900c:	e5933000 	ldr	r3, [r3]
40009010:	e50b3034 	str	r3, [fp, #-52]	@ 0xffffffcc
40009014:	e24b305c 	sub	r3, fp, #92	@ 0x5c
40009018:	e3a0200a 	mov	r2, #10
4000901c:	e1a01003 	mov	r1, r3
40009020:	e51b0034 	ldr	r0, [fp, #-52]	@ 0xffffffcc
40009024:	ebfffdda 	bl	40008794 <itoa>
40009028:	e24b305c 	sub	r3, fp, #92	@ 0x5c
4000902c:	e50b300c 	str	r3, [fp, #-12]
40009030:	ea000006 	b	40009050 <vstrfmt+0x188>
40009034:	e51b300c 	ldr	r3, [fp, #-12]
40009038:	e2832001 	add	r2, r3, #1
4000903c:	e50b200c 	str	r2, [fp, #-12]
40009040:	e5d32000 	ldrb	r2, [r3]
40009044:	e51b3060 	ldr	r3, [fp, #-96]	@ 0xffffffa0
40009048:	e1a00002 	mov	r0, r2
4000904c:	e12fff33 	blx	r3
40009050:	e51b300c 	ldr	r3, [fp, #-12]
40009054:	e5d33000 	ldrb	r3, [r3]
40009058:	e3530000 	cmp	r3, #0
4000905c:	1afffff4 	bne	40009034 <vstrfmt+0x16c>
40009060:	ea00008b 	b	40009294 <vstrfmt+0x3cc>
40009064:	e51b3068 	ldr	r3, [fp, #-104]	@ 0xffffff98
40009068:	e2832004 	add	r2, r3, #4
4000906c:	e50b2068 	str	r2, [fp, #-104]	@ 0xffffff98
40009070:	e5933000 	ldr	r3, [r3]
40009074:	e50b3028 	str	r3, [fp, #-40]	@ 0xffffffd8
40009078:	e24b305c 	sub	r3, fp, #92	@ 0x5c
4000907c:	e3a0200a 	mov	r2, #10
40009080:	e1a01003 	mov	r1, r3
40009084:	e51b0028 	ldr	r0, [fp, #-40]	@ 0xffffffd8
40009088:	ebfffe20 	bl	40008910 <utoa>
4000908c:	e24b305c 	sub	r3, fp, #92	@ 0x5c
40009090:	e50b3010 	str	r3, [fp, #-16]
40009094:	ea000006 	b	400090b4 <vstrfmt+0x1ec>
40009098:	e51b3010 	ldr	r3, [fp, #-16]
4000909c:	e2832001 	add	r2, r3, #1
400090a0:	e50b2010 	str	r2, [fp, #-16]
400090a4:	e5d32000 	ldrb	r2, [r3]
400090a8:	e51b3060 	ldr	r3, [fp, #-96]	@ 0xffffffa0
400090ac:	e1a00002 	mov	r0, r2
400090b0:	e12fff33 	blx	r3
400090b4:	e51b3010 	ldr	r3, [fp, #-16]
400090b8:	e5d33000 	ldrb	r3, [r3]
400090bc:	e3530000 	cmp	r3, #0
400090c0:	1afffff4 	bne	40009098 <vstrfmt+0x1d0>
400090c4:	ea000072 	b	40009294 <vstrfmt+0x3cc>
400090c8:	e51b3068 	ldr	r3, [fp, #-104]	@ 0xffffff98
400090cc:	e2832004 	add	r2, r3, #4
400090d0:	e50b2068 	str	r2, [fp, #-104]	@ 0xffffff98
400090d4:	e5933000 	ldr	r3, [r3]
400090d8:	e50b302c 	str	r3, [fp, #-44]	@ 0xffffffd4
400090dc:	e51b302c 	ldr	r3, [fp, #-44]	@ 0xffffffd4
400090e0:	e24b105c 	sub	r1, fp, #92	@ 0x5c
400090e4:	e3a02010 	mov	r2, #16
400090e8:	e1a00003 	mov	r0, r3
400090ec:	ebfffe07 	bl	40008910 <utoa>
400090f0:	e24b305c 	sub	r3, fp, #92	@ 0x5c
400090f4:	e50b3014 	str	r3, [fp, #-20]	@ 0xffffffec
400090f8:	e51b3060 	ldr	r3, [fp, #-96]	@ 0xffffffa0
400090fc:	e3a00030 	mov	r0, #48	@ 0x30
40009100:	e12fff33 	blx	r3
40009104:	e51b3060 	ldr	r3, [fp, #-96]	@ 0xffffffa0
40009108:	e3a00078 	mov	r0, #120	@ 0x78
4000910c:	e12fff33 	blx	r3
40009110:	ea000006 	b	40009130 <vstrfmt+0x268>
40009114:	e51b3014 	ldr	r3, [fp, #-20]	@ 0xffffffec
40009118:	e2832001 	add	r2, r3, #1
4000911c:	e50b2014 	str	r2, [fp, #-20]	@ 0xffffffec
40009120:	e5d32000 	ldrb	r2, [r3]
40009124:	e51b3060 	ldr	r3, [fp, #-96]	@ 0xffffffa0
40009128:	e1a00002 	mov	r0, r2
4000912c:	e12fff33 	blx	r3
40009130:	e51b3014 	ldr	r3, [fp, #-20]	@ 0xffffffec
40009134:	e5d33000 	ldrb	r3, [r3]
40009138:	e3530000 	cmp	r3, #0
4000913c:	1afffff4 	bne	40009114 <vstrfmt+0x24c>
40009140:	ea000053 	b	40009294 <vstrfmt+0x3cc>
40009144:	e51b3068 	ldr	r3, [fp, #-104]	@ 0xffffff98
40009148:	e2832004 	add	r2, r3, #4
4000914c:	e50b2068 	str	r2, [fp, #-104]	@ 0xffffff98
40009150:	e5933000 	ldr	r3, [r3]
40009154:	e50b3024 	str	r3, [fp, #-36]	@ 0xffffffdc
40009158:	e24b305c 	sub	r3, fp, #92	@ 0x5c
4000915c:	e3a02010 	mov	r2, #16
40009160:	e1a01003 	mov	r1, r3
40009164:	e51b0024 	ldr	r0, [fp, #-36]	@ 0xffffffdc
40009168:	ebfffde8 	bl	40008910 <utoa>
4000916c:	e24b305c 	sub	r3, fp, #92	@ 0x5c
40009170:	e50b3018 	str	r3, [fp, #-24]	@ 0xffffffe8
40009174:	ea000006 	b	40009194 <vstrfmt+0x2cc>
40009178:	e51b3018 	ldr	r3, [fp, #-24]	@ 0xffffffe8
4000917c:	e2832001 	add	r2, r3, #1
40009180:	e50b2018 	str	r2, [fp, #-24]	@ 0xffffffe8
40009184:	e5d32000 	ldrb	r2, [r3]
40009188:	e51b3060 	ldr	r3, [fp, #-96]	@ 0xffffffa0
4000918c:	e1a00002 	mov	r0, r2
40009190:	e12fff33 	blx	r3
40009194:	e51b3018 	ldr	r3, [fp, #-24]	@ 0xffffffe8
40009198:	e5d33000 	ldrb	r3, [r3]
4000919c:	e3530000 	cmp	r3, #0
400091a0:	1afffff4 	bne	40009178 <vstrfmt+0x2b0>
400091a4:	ea00003a 	b	40009294 <vstrfmt+0x3cc>
400091a8:	e51b3068 	ldr	r3, [fp, #-104]	@ 0xffffff98
400091ac:	e2832004 	add	r2, r3, #4
400091b0:	e50b2068 	str	r2, [fp, #-104]	@ 0xffffff98
400091b4:	e5933000 	ldr	r3, [r3]
400091b8:	e50b3030 	str	r3, [fp, #-48]	@ 0xffffffd0
400091bc:	e24b305c 	sub	r3, fp, #92	@ 0x5c
400091c0:	e3a02008 	mov	r2, #8
400091c4:	e1a01003 	mov	r1, r3
400091c8:	e51b0030 	ldr	r0, [fp, #-48]	@ 0xffffffd0
400091cc:	ebfffdcf 	bl	40008910 <utoa>
400091d0:	e24b305c 	sub	r3, fp, #92	@ 0x5c
400091d4:	e50b301c 	str	r3, [fp, #-28]	@ 0xffffffe4
400091d8:	ea000006 	b	400091f8 <vstrfmt+0x330>
400091dc:	e51b301c 	ldr	r3, [fp, #-28]	@ 0xffffffe4
400091e0:	e2832001 	add	r2, r3, #1
400091e4:	e50b201c 	str	r2, [fp, #-28]	@ 0xffffffe4
400091e8:	e5d32000 	ldrb	r2, [r3]
400091ec:	e51b3060 	ldr	r3, [fp, #-96]	@ 0xffffffa0
400091f0:	e1a00002 	mov	r0, r2
400091f4:	e12fff33 	blx	r3
400091f8:	e51b301c 	ldr	r3, [fp, #-28]	@ 0xffffffe4
400091fc:	e5d33000 	ldrb	r3, [r3]
40009200:	e3530000 	cmp	r3, #0
40009204:	1afffff4 	bne	400091dc <vstrfmt+0x314>
40009208:	ea000021 	b	40009294 <vstrfmt+0x3cc>
4000920c:	e51b3068 	ldr	r3, [fp, #-104]	@ 0xffffff98
40009210:	e2832004 	add	r2, r3, #4
40009214:	e50b2068 	str	r2, [fp, #-104]	@ 0xffffff98
40009218:	e5933000 	ldr	r3, [r3]
4000921c:	e50b303c 	str	r3, [fp, #-60]	@ 0xffffffc4
40009220:	e51b303c 	ldr	r3, [fp, #-60]	@ 0xffffffc4
40009224:	e24b105c 	sub	r1, fp, #92	@ 0x5c
40009228:	e3a02002 	mov	r2, #2
4000922c:	e1a00003 	mov	r0, r3
40009230:	ebfffdb6 	bl	40008910 <utoa>
40009234:	e24b305c 	sub	r3, fp, #92	@ 0x5c
40009238:	e50b3020 	str	r3, [fp, #-32]	@ 0xffffffe0
4000923c:	ea000006 	b	4000925c <vstrfmt+0x394>
40009240:	e51b3020 	ldr	r3, [fp, #-32]	@ 0xffffffe0
40009244:	e2832001 	add	r2, r3, #1
40009248:	e50b2020 	str	r2, [fp, #-32]	@ 0xffffffe0
4000924c:	e5d32000 	ldrb	r2, [r3]
40009250:	e51b3060 	ldr	r3, [fp, #-96]	@ 0xffffffa0
40009254:	e1a00002 	mov	r0, r2
40009258:	e12fff33 	blx	r3
4000925c:	e51b3020 	ldr	r3, [fp, #-32]	@ 0xffffffe0
40009260:	e5d33000 	ldrb	r3, [r3]
40009264:	e3530000 	cmp	r3, #0
40009268:	1afffff4 	bne	40009240 <vstrfmt+0x378>
4000926c:	ea000008 	b	40009294 <vstrfmt+0x3cc>
40009270:	e51b3060 	ldr	r3, [fp, #-96]	@ 0xffffffa0
40009274:	e3a00025 	mov	r0, #37	@ 0x25
40009278:	e12fff33 	blx	r3
4000927c:	ea000004 	b	40009294 <vstrfmt+0x3cc>
40009280:	e51b3064 	ldr	r3, [fp, #-100]	@ 0xffffff9c
40009284:	e5d32000 	ldrb	r2, [r3]
40009288:	e51b3060 	ldr	r3, [fp, #-96]	@ 0xffffffa0
4000928c:	e1a00002 	mov	r0, r2
40009290:	e12fff33 	blx	r3
40009294:	e51b3064 	ldr	r3, [fp, #-100]	@ 0xffffff9c
40009298:	e2833001 	add	r3, r3, #1
4000929c:	e50b3064 	str	r3, [fp, #-100]	@ 0xffffff9c
400092a0:	ea000006 	b	400092c0 <vstrfmt+0x3f8>
400092a4:	e51b3064 	ldr	r3, [fp, #-100]	@ 0xffffff9c
400092a8:	e2832001 	add	r2, r3, #1
400092ac:	e50b2064 	str	r2, [fp, #-100]	@ 0xffffff9c
400092b0:	e5d32000 	ldrb	r2, [r3]
400092b4:	e51b3060 	ldr	r3, [fp, #-96]	@ 0xffffffa0
400092b8:	e1a00002 	mov	r0, r2
400092bc:	e12fff33 	blx	r3
400092c0:	e51b3064 	ldr	r3, [fp, #-100]	@ 0xffffff9c
400092c4:	e5d33000 	ldrb	r3, [r3]
400092c8:	e3530000 	cmp	r3, #0
400092cc:	1affff05 	bne	40008ee8 <vstrfmt+0x20>
400092d0:	e320f000 	nop	{0}
400092d4:	e320f000 	nop	{0}
400092d8:	e24bd004 	sub	sp, fp, #4
400092dc:	e59db000 	ldr	fp, [sp]
400092e0:	e28dd004 	add	sp, sp, #4
400092e4:	e49df004 	pop	{pc}		@ (ldr pc, [sp], #4)

400092e8 <early>:
400092e8:	e52db008 	str	fp, [sp, #-8]!
400092ec:	e58de004 	str	lr, [sp, #4]
400092f0:	e28db004 	add	fp, sp, #4
400092f4:	e24dd038 	sub	sp, sp, #56	@ 0x38
400092f8:	e50b0038 	str	r0, [fp, #-56]	@ 0xffffffc8
400092fc:	e51b0038 	ldr	r0, [fp, #-56]	@ 0xffffffc8
40009300:	eb000153 	bl	40009854 <dtb_parse>
40009304:	e1a02000 	mov	r2, r0
40009308:	e30b3bac 	movw	r3, #48044	@ 0xbbac
4000930c:	e3443000 	movt	r3, #16384	@ 0x4000
40009310:	e5832000 	str	r2, [r3]
40009314:	e30b3bac 	movw	r3, #48044	@ 0xbbac
40009318:	e3443000 	movt	r3, #16384	@ 0x4000
4000931c:	e5933000 	ldr	r3, [r3]
40009320:	e30b10d8 	movw	r1, #45272	@ 0xb0d8
40009324:	e3441000 	movt	r1, #16384	@ 0x4000
40009328:	e1a00003 	mov	r0, r3
4000932c:	eb0003ca 	bl	4000a25c <dtb_get_reg_addr>
40009330:	e50b0010 	str	r0, [fp, #-16]
40009334:	e30b3bac 	movw	r3, #48044	@ 0xbbac
40009338:	e3443000 	movt	r3, #16384	@ 0x4000
4000933c:	e5933000 	ldr	r3, [r3]
40009340:	e30b10d8 	movw	r1, #45272	@ 0xb0d8
40009344:	e3441000 	movt	r1, #16384	@ 0x4000
40009348:	e1a00003 	mov	r0, r3
4000934c:	eb000410 	bl	4000a394 <dtb_get_reg_size>
40009350:	e50b0014 	str	r0, [fp, #-20]	@ 0xffffffec
40009354:	e51b2010 	ldr	r2, [fp, #-16]
40009358:	e51b3014 	ldr	r3, [fp, #-20]	@ 0xffffffec
4000935c:	e0823003 	add	r3, r2, r3
40009360:	e50b3018 	str	r3, [fp, #-24]	@ 0xffffffe8
40009364:	e3082000 	movw	r2, #32768	@ 0x8000
40009368:	e3442000 	movt	r2, #16384	@ 0x4000
4000936c:	e30b3bb0 	movw	r3, #48048	@ 0xbbb0
40009370:	e3443000 	movt	r3, #16384	@ 0x4000
40009374:	e5832000 	str	r2, [r3]
40009378:	e30c2208 	movw	r2, #49672	@ 0xc208
4000937c:	e3442001 	movt	r2, #16385	@ 0x4001
40009380:	e30b3bb0 	movw	r3, #48048	@ 0xbbb0
40009384:	e3443000 	movt	r3, #16384	@ 0x4000
40009388:	e5832004 	str	r2, [r3, #4]
4000938c:	e30e2fff 	movw	r2, #61439	@ 0xefff
40009390:	e34420ff 	movt	r2, #16639	@ 0x40ff
40009394:	e30b3bb0 	movw	r3, #48048	@ 0xbbb0
40009398:	e3443000 	movt	r3, #16384	@ 0x4000
4000939c:	e583200c 	str	r2, [r3, #12]
400093a0:	e30f2fff 	movw	r2, #65535	@ 0xffff
400093a4:	e34420ff 	movt	r2, #16639	@ 0x40ff
400093a8:	e30b3bb0 	movw	r3, #48048	@ 0xbbb0
400093ac:	e3443000 	movt	r3, #16384	@ 0x4000
400093b0:	e5832008 	str	r2, [r3, #8]
400093b4:	e30b3a94 	movw	r3, #47764	@ 0xba94
400093b8:	e3443000 	movt	r3, #16384	@ 0x4000
400093bc:	e51b2010 	ldr	r2, [fp, #-16]
400093c0:	e5832000 	str	r2, [r3]
400093c4:	e30b3a94 	movw	r3, #47764	@ 0xba94
400093c8:	e3443000 	movt	r3, #16384	@ 0x4000
400093cc:	e51b2018 	ldr	r2, [fp, #-24]	@ 0xffffffe8
400093d0:	e5832004 	str	r2, [r3, #4]
400093d4:	e30b3a94 	movw	r3, #47764	@ 0xba94
400093d8:	e3443000 	movt	r3, #16384	@ 0x4000
400093dc:	e5933000 	ldr	r3, [r3]
400093e0:	e1a02623 	lsr	r2, r3, #12
400093e4:	e30c31f0 	movw	r3, #49648	@ 0xc1f0
400093e8:	e3443001 	movt	r3, #16385	@ 0x4001
400093ec:	e5832000 	str	r2, [r3]
400093f0:	e30b3a94 	movw	r3, #47764	@ 0xba94
400093f4:	e3443000 	movt	r3, #16384	@ 0x4000
400093f8:	e5933004 	ldr	r3, [r3, #4]
400093fc:	e1a02623 	lsr	r2, r3, #12
40009400:	e30c31f0 	movw	r3, #49648	@ 0xc1f0
40009404:	e3443001 	movt	r3, #16385	@ 0x4001
40009408:	e5832004 	str	r2, [r3, #4]
4000940c:	e30c31f0 	movw	r3, #49648	@ 0xc1f0
40009410:	e3443001 	movt	r3, #16385	@ 0x4001
40009414:	e5932004 	ldr	r2, [r3, #4]
40009418:	e30c31f0 	movw	r3, #49648	@ 0xc1f0
4000941c:	e3443001 	movt	r3, #16385	@ 0x4001
40009420:	e5933000 	ldr	r3, [r3]
40009424:	e0422003 	sub	r2, r2, r3
40009428:	e30c31f0 	movw	r3, #49648	@ 0xc1f0
4000942c:	e3443001 	movt	r3, #16385	@ 0x4001
40009430:	e5832008 	str	r2, [r3, #8]
40009434:	e30c31f0 	movw	r3, #49648	@ 0xc1f0
40009438:	e3443001 	movt	r3, #16385	@ 0x4001
4000943c:	e5932008 	ldr	r2, [r3, #8]
40009440:	e30c31f0 	movw	r3, #49648	@ 0xc1f0
40009444:	e3443001 	movt	r3, #16385	@ 0x4001
40009448:	e583200c 	str	r2, [r3, #12]
4000944c:	e30b3bb0 	movw	r3, #48048	@ 0xbbb0
40009450:	e3443000 	movt	r3, #16384	@ 0x4000
40009454:	e5933004 	ldr	r3, [r3, #4]
40009458:	e3a01a01 	mov	r1, #4096	@ 0x1000
4000945c:	e1a00003 	mov	r0, r3
40009460:	ebfffc4d 	bl	4000859c <align_up>
40009464:	e50b001c 	str	r0, [fp, #-28]	@ 0xffffffe4
40009468:	e30c31f0 	movw	r3, #49648	@ 0xc1f0
4000946c:	e3443001 	movt	r3, #16385	@ 0x4001
40009470:	e5933008 	ldr	r3, [r3, #8]
40009474:	e2833007 	add	r3, r3, #7
40009478:	e1a031a3 	lsr	r3, r3, #3
4000947c:	e50b3020 	str	r3, [fp, #-32]	@ 0xffffffe0
40009480:	e3a01a01 	mov	r1, #4096	@ 0x1000
40009484:	e51b0020 	ldr	r0, [fp, #-32]	@ 0xffffffe0
40009488:	ebfffc43 	bl	4000859c <align_up>
4000948c:	e50b0024 	str	r0, [fp, #-36]	@ 0xffffffdc
40009490:	e51b201c 	ldr	r2, [fp, #-28]	@ 0xffffffe4
40009494:	e51b3024 	ldr	r3, [fp, #-36]	@ 0xffffffdc
40009498:	e0823003 	add	r3, r2, r3
4000949c:	e50b3028 	str	r3, [fp, #-40]	@ 0xffffffd8
400094a0:	e30b3bb0 	movw	r3, #48048	@ 0xbbb0
400094a4:	e3443000 	movt	r3, #16384	@ 0x4000
400094a8:	e593300c 	ldr	r3, [r3, #12]
400094ac:	e51b2028 	ldr	r2, [fp, #-40]	@ 0xffffffd8
400094b0:	e1520003 	cmp	r2, r3
400094b4:	9a000002 	bls	400094c4 <early+0x1dc>
400094b8:	e30b00e0 	movw	r0, #45280	@ 0xb0e0
400094bc:	e3440000 	movt	r0, #16384	@ 0x4000
400094c0:	ebfffb2d 	bl	4000817c <panic>
400094c4:	e51b201c 	ldr	r2, [fp, #-28]	@ 0xffffffe4
400094c8:	e30c31f0 	movw	r3, #49648	@ 0xc1f0
400094cc:	e3443001 	movt	r3, #16385	@ 0x4001
400094d0:	e5832010 	str	r2, [r3, #16]
400094d4:	e30c31f0 	movw	r3, #49648	@ 0xc1f0
400094d8:	e3443001 	movt	r3, #16385	@ 0x4001
400094dc:	e51b2020 	ldr	r2, [fp, #-32]	@ 0xffffffe0
400094e0:	e5832014 	str	r2, [r3, #20]
400094e4:	e51b301c 	ldr	r3, [fp, #-28]	@ 0xffffffe4
400094e8:	e51b2024 	ldr	r2, [fp, #-36]	@ 0xffffffdc
400094ec:	e3a01000 	mov	r1, #0
400094f0:	e1a00003 	mov	r0, r3
400094f4:	ebfffbbe 	bl	400083f4 <memset>
400094f8:	e30b3bb0 	movw	r3, #48048	@ 0xbbb0
400094fc:	e3443000 	movt	r3, #16384	@ 0x4000
40009500:	e51b201c 	ldr	r2, [fp, #-28]	@ 0xffffffe4
40009504:	e5832010 	str	r2, [r3, #16]
40009508:	e30b3bb0 	movw	r3, #48048	@ 0xbbb0
4000950c:	e3443000 	movt	r3, #16384	@ 0x4000
40009510:	e51b2028 	ldr	r2, [fp, #-40]	@ 0xffffffd8
40009514:	e5832014 	str	r2, [r3, #20]
40009518:	e30b3bb0 	movw	r3, #48048	@ 0xbbb0
4000951c:	e3443000 	movt	r3, #16384	@ 0x4000
40009520:	e5932000 	ldr	r2, [r3]
40009524:	e30b3bb0 	movw	r3, #48048	@ 0xbbb0
40009528:	e3443000 	movt	r3, #16384	@ 0x4000
4000952c:	e5933004 	ldr	r3, [r3, #4]
40009530:	e1a01003 	mov	r1, r3
40009534:	e1a00002 	mov	r0, r2
40009538:	eb000400 	bl	4000a540 <mark>
4000953c:	e30b3bb0 	movw	r3, #48048	@ 0xbbb0
40009540:	e3443000 	movt	r3, #16384	@ 0x4000
40009544:	e5932010 	ldr	r2, [r3, #16]
40009548:	e30b3bb0 	movw	r3, #48048	@ 0xbbb0
4000954c:	e3443000 	movt	r3, #16384	@ 0x4000
40009550:	e5933014 	ldr	r3, [r3, #20]
40009554:	e1a01003 	mov	r1, r3
40009558:	e1a00002 	mov	r0, r2
4000955c:	eb0003f7 	bl	4000a540 <mark>
40009560:	e3a03002 	mov	r3, #2
40009564:	e50b302c 	str	r3, [fp, #-44]	@ 0xffffffd4
40009568:	e3a03000 	mov	r3, #0
4000956c:	e50b3008 	str	r3, [fp, #-8]
40009570:	e3a03000 	mov	r3, #0
40009574:	e50b300c 	str	r3, [fp, #-12]
40009578:	ea00000c 	b	400095b0 <early+0x2c8>
4000957c:	eb0004e2 	bl	4000a90c <alloc_page>
40009580:	e50b0034 	str	r0, [fp, #-52]	@ 0xffffffcc
40009584:	e51b3034 	ldr	r3, [fp, #-52]	@ 0xffffffcc
40009588:	e3530000 	cmp	r3, #0
4000958c:	1a000002 	bne	4000959c <early+0x2b4>
40009590:	e30b0108 	movw	r0, #45320	@ 0xb108
40009594:	e3440000 	movt	r0, #16384	@ 0x4000
40009598:	ebfffaf7 	bl	4000817c <panic>
4000959c:	e51b3034 	ldr	r3, [fp, #-52]	@ 0xffffffcc
400095a0:	e50b3008 	str	r3, [fp, #-8]
400095a4:	e51b300c 	ldr	r3, [fp, #-12]
400095a8:	e2833001 	add	r3, r3, #1
400095ac:	e50b300c 	str	r3, [fp, #-12]
400095b0:	e51b200c 	ldr	r2, [fp, #-12]
400095b4:	e51b302c 	ldr	r3, [fp, #-44]	@ 0xffffffd4
400095b8:	e1520003 	cmp	r2, r3
400095bc:	3affffee 	bcc	4000957c <early+0x294>
400095c0:	e51b302c 	ldr	r3, [fp, #-44]	@ 0xffffffd4
400095c4:	e1a03603 	lsl	r3, r3, #12
400095c8:	e51b2008 	ldr	r2, [fp, #-8]
400095cc:	e0823003 	add	r3, r2, r3
400095d0:	e50b3030 	str	r3, [fp, #-48]	@ 0xffffffd0
400095d4:	e51b3030 	ldr	r3, [fp, #-48]	@ 0xffffffd0
400095d8:	e1a0d003 	mov	sp, r3
400095dc:	e30b3bb0 	movw	r3, #48048	@ 0xbbb0
400095e0:	e3443000 	movt	r3, #16384	@ 0x4000
400095e4:	e51b2008 	ldr	r2, [fp, #-8]
400095e8:	e5832008 	str	r2, [r3, #8]
400095ec:	e30b3bb0 	movw	r3, #48048	@ 0xbbb0
400095f0:	e3443000 	movt	r3, #16384	@ 0x4000
400095f4:	e51b2030 	ldr	r2, [fp, #-48]	@ 0xffffffd0
400095f8:	e583200c 	str	r2, [r3, #12]
400095fc:	e30b3bb0 	movw	r3, #48048	@ 0xbbb0
40009600:	e3443000 	movt	r3, #16384	@ 0x4000
40009604:	e5932008 	ldr	r2, [r3, #8]
40009608:	e30b3bb0 	movw	r3, #48048	@ 0xbbb0
4000960c:	e3443000 	movt	r3, #16384	@ 0x4000
40009610:	e593300c 	ldr	r3, [r3, #12]
40009614:	e1a01003 	mov	r1, r3
40009618:	e1a00002 	mov	r0, r2
4000961c:	eb0003c7 	bl	4000a540 <mark>
40009620:	e30b3bac 	movw	r3, #48044	@ 0xbbac
40009624:	e3443000 	movt	r3, #16384	@ 0x4000
40009628:	e5933000 	ldr	r3, [r3]
4000962c:	e30b1128 	movw	r1, #45352	@ 0xb128
40009630:	e3441000 	movt	r1, #16384	@ 0x4000
40009634:	e1a00003 	mov	r0, r3
40009638:	eb000307 	bl	4000a25c <dtb_get_reg_addr>
4000963c:	e1a03000 	mov	r3, r0
40009640:	e1a00003 	mov	r0, r3
40009644:	ebfffa7e 	bl	40008044 <uart_init>
40009648:	ebfffb3f 	bl	4000834c <enable_interrupts>
4000964c:	eb00061e 	bl	4000aecc <kmain>
40009650:	e320f000 	nop	{0}
40009654:	e24bd004 	sub	sp, fp, #4
40009658:	e59db000 	ldr	fp, [sp]
4000965c:	e28dd004 	add	sp, sp, #4
40009660:	e49df004 	pop	{pc}		@ (ldr pc, [sp], #4)

40009664 <halt>:
40009664:	e52db004 	push	{fp}		@ (str fp, [sp, #-4]!)
40009668:	e28db000 	add	fp, sp, #0
4000966c:	e320f003 	wfi
40009670:	eafffffd 	b	4000966c <halt+0x8>

40009674 <bswap32>:
40009674:	e52db004 	push	{fp}		@ (str fp, [sp, #-4]!)
40009678:	e28db000 	add	fp, sp, #0
4000967c:	e24dd00c 	sub	sp, sp, #12
40009680:	e50b0008 	str	r0, [fp, #-8]
40009684:	e51b3008 	ldr	r3, [fp, #-8]
40009688:	e6bf3f33 	rev	r3, r3
4000968c:	e1a00003 	mov	r0, r3
40009690:	e28bd000 	add	sp, fp, #0
40009694:	e49db004 	pop	{fp}		@ (ldr fp, [sp], #4)
40009698:	e12fff1e 	bx	lr

4000969c <dtb_new_node>:
4000969c:	e52db008 	str	fp, [sp, #-8]!
400096a0:	e58de004 	str	lr, [sp, #4]
400096a4:	e28db004 	add	fp, sp, #4
400096a8:	e24dd010 	sub	sp, sp, #16
400096ac:	e30c31d0 	movw	r3, #49616	@ 0xc1d0
400096b0:	e3443001 	movt	r3, #16385	@ 0x4001
400096b4:	e5933000 	ldr	r3, [r3]
400096b8:	e353007f 	cmp	r3, #127	@ 0x7f
400096bc:	9a000000 	bls	400096c4 <dtb_new_node+0x28>
400096c0:	ebffffe7 	bl	40009664 <halt>
400096c4:	e30c31d0 	movw	r3, #49616	@ 0xc1d0
400096c8:	e3443001 	movt	r3, #16385	@ 0x4001
400096cc:	e5932000 	ldr	r2, [r3]
400096d0:	e2821001 	add	r1, r2, #1
400096d4:	e30c31d0 	movw	r3, #49616	@ 0xc1d0
400096d8:	e3443001 	movt	r3, #16385	@ 0x4001
400096dc:	e5831000 	str	r1, [r3]
400096e0:	e3a03f83 	mov	r3, #524	@ 0x20c
400096e4:	e0020293 	mul	r2, r3, r2
400096e8:	e30b3bd0 	movw	r3, #48080	@ 0xbbd0
400096ec:	e3443000 	movt	r3, #16384	@ 0x4000
400096f0:	e0823003 	add	r3, r2, r3
400096f4:	e50b3010 	str	r3, [fp, #-16]
400096f8:	e51b3010 	ldr	r3, [fp, #-16]
400096fc:	e3a02000 	mov	r2, #0
40009700:	e5832000 	str	r2, [r3]
40009704:	e51b3010 	ldr	r3, [fp, #-16]
40009708:	e3a02000 	mov	r2, #0
4000970c:	e5832184 	str	r2, [r3, #388]	@ 0x184
40009710:	e51b3010 	ldr	r3, [fp, #-16]
40009714:	e3a02000 	mov	r2, #0
40009718:	e5832208 	str	r2, [r3, #520]	@ 0x208
4000971c:	e3a03000 	mov	r3, #0
40009720:	e50b3008 	str	r3, [fp, #-8]
40009724:	ea000007 	b	40009748 <dtb_new_node+0xac>
40009728:	e51b3010 	ldr	r3, [fp, #-16]
4000972c:	e51b2008 	ldr	r2, [fp, #-8]
40009730:	e2822062 	add	r2, r2, #98	@ 0x62
40009734:	e3a01000 	mov	r1, #0
40009738:	e7831102 	str	r1, [r3, r2, lsl #2]
4000973c:	e51b3008 	ldr	r3, [fp, #-8]
40009740:	e2833001 	add	r3, r3, #1
40009744:	e50b3008 	str	r3, [fp, #-8]
40009748:	e51b3008 	ldr	r3, [fp, #-8]
4000974c:	e353001f 	cmp	r3, #31
40009750:	9afffff4 	bls	40009728 <dtb_new_node+0x8c>
40009754:	e3a03000 	mov	r3, #0
40009758:	e50b300c 	str	r3, [fp, #-12]
4000975c:	ea000020 	b	400097e4 <dtb_new_node+0x148>
40009760:	e51b1010 	ldr	r1, [fp, #-16]
40009764:	e51b200c 	ldr	r2, [fp, #-12]
40009768:	e1a03002 	mov	r3, r2
4000976c:	e1a03083 	lsl	r3, r3, #1
40009770:	e0833002 	add	r3, r3, r2
40009774:	e1a03103 	lsl	r3, r3, #2
40009778:	e0813003 	add	r3, r1, r3
4000977c:	e2833004 	add	r3, r3, #4
40009780:	e3a02000 	mov	r2, #0
40009784:	e5832000 	str	r2, [r3]
40009788:	e51b1010 	ldr	r1, [fp, #-16]
4000978c:	e51b200c 	ldr	r2, [fp, #-12]
40009790:	e1a03002 	mov	r3, r2
40009794:	e1a03083 	lsl	r3, r3, #1
40009798:	e0833002 	add	r3, r3, r2
4000979c:	e1a03103 	lsl	r3, r3, #2
400097a0:	e0813003 	add	r3, r1, r3
400097a4:	e2833008 	add	r3, r3, #8
400097a8:	e3a02000 	mov	r2, #0
400097ac:	e5832000 	str	r2, [r3]
400097b0:	e51b1010 	ldr	r1, [fp, #-16]
400097b4:	e51b200c 	ldr	r2, [fp, #-12]
400097b8:	e1a03002 	mov	r3, r2
400097bc:	e1a03083 	lsl	r3, r3, #1
400097c0:	e0833002 	add	r3, r3, r2
400097c4:	e1a03103 	lsl	r3, r3, #2
400097c8:	e0813003 	add	r3, r1, r3
400097cc:	e283300c 	add	r3, r3, #12
400097d0:	e3a02000 	mov	r2, #0
400097d4:	e5832000 	str	r2, [r3]
400097d8:	e51b300c 	ldr	r3, [fp, #-12]
400097dc:	e2833001 	add	r3, r3, #1
400097e0:	e50b300c 	str	r3, [fp, #-12]
400097e4:	e51b300c 	ldr	r3, [fp, #-12]
400097e8:	e353001f 	cmp	r3, #31
400097ec:	9affffdb 	bls	40009760 <dtb_new_node+0xc4>
400097f0:	e51b3010 	ldr	r3, [fp, #-16]
400097f4:	e1a00003 	mov	r0, r3
400097f8:	e24bd004 	sub	sp, fp, #4
400097fc:	e59db000 	ldr	fp, [sp]
40009800:	e28dd004 	add	sp, sp, #4
40009804:	e49df004 	pop	{pc}		@ (ldr pc, [sp], #4)

40009808 <read_be32>:
40009808:	e52db008 	str	fp, [sp, #-8]!
4000980c:	e58de004 	str	lr, [sp, #4]
40009810:	e28db004 	add	fp, sp, #4
40009814:	e24dd010 	sub	sp, sp, #16
40009818:	e50b0010 	str	r0, [fp, #-16]
4000981c:	e24b3008 	sub	r3, fp, #8
40009820:	e3a02004 	mov	r2, #4
40009824:	e51b1010 	ldr	r1, [fp, #-16]
40009828:	e1a00003 	mov	r0, r3
4000982c:	ebfffacd 	bl	40008368 <memcpy>
40009830:	e51b3008 	ldr	r3, [fp, #-8]
40009834:	e1a00003 	mov	r0, r3
40009838:	ebffff8d 	bl	40009674 <bswap32>
4000983c:	e1a03000 	mov	r3, r0
40009840:	e1a00003 	mov	r0, r3
40009844:	e24bd004 	sub	sp, fp, #4
40009848:	e59db000 	ldr	fp, [sp]
4000984c:	e28dd004 	add	sp, sp, #4
40009850:	e49df004 	pop	{pc}		@ (ldr pc, [sp], #4)

40009854 <dtb_parse>:
40009854:	e52db008 	str	fp, [sp, #-8]!
40009858:	e58de004 	str	lr, [sp, #4]
4000985c:	e28db004 	add	fp, sp, #4
40009860:	e24ddf5e 	sub	sp, sp, #376	@ 0x178
40009864:	e50b0178 	str	r0, [fp, #-376]	@ 0xfffffe88
40009868:	e51b3178 	ldr	r3, [fp, #-376]	@ 0xfffffe88
4000986c:	e3530000 	cmp	r3, #0
40009870:	1a000000 	bne	40009878 <dtb_parse+0x24>
40009874:	ebffff7a 	bl	40009664 <halt>
40009878:	e51b3178 	ldr	r3, [fp, #-376]	@ 0xfffffe88
4000987c:	e50b3024 	str	r3, [fp, #-36]	@ 0xffffffdc
40009880:	e51b3024 	ldr	r3, [fp, #-36]	@ 0xffffffdc
40009884:	e5933000 	ldr	r3, [r3]
40009888:	e1a00003 	mov	r0, r3
4000988c:	ebffff78 	bl	40009674 <bswap32>
40009890:	e50b0028 	str	r0, [fp, #-40]	@ 0xffffffd8
40009894:	e51b2028 	ldr	r2, [fp, #-40]	@ 0xffffffd8
40009898:	e30f3eed 	movw	r3, #65261	@ 0xfeed
4000989c:	e34d300d 	movt	r3, #53261	@ 0xd00d
400098a0:	e1520003 	cmp	r2, r3
400098a4:	0a000000 	beq	400098ac <dtb_parse+0x58>
400098a8:	ebffff6d 	bl	40009664 <halt>
400098ac:	e51b3024 	ldr	r3, [fp, #-36]	@ 0xffffffdc
400098b0:	e5933004 	ldr	r3, [r3, #4]
400098b4:	e1a00003 	mov	r0, r3
400098b8:	ebffff6d 	bl	40009674 <bswap32>
400098bc:	e50b002c 	str	r0, [fp, #-44]	@ 0xffffffd4
400098c0:	e51b3024 	ldr	r3, [fp, #-36]	@ 0xffffffdc
400098c4:	e5933008 	ldr	r3, [r3, #8]
400098c8:	e1a00003 	mov	r0, r3
400098cc:	ebffff68 	bl	40009674 <bswap32>
400098d0:	e50b0030 	str	r0, [fp, #-48]	@ 0xffffffd0
400098d4:	e51b3024 	ldr	r3, [fp, #-36]	@ 0xffffffdc
400098d8:	e593300c 	ldr	r3, [r3, #12]
400098dc:	e1a00003 	mov	r0, r3
400098e0:	ebffff63 	bl	40009674 <bswap32>
400098e4:	e50b0034 	str	r0, [fp, #-52]	@ 0xffffffcc
400098e8:	e51b3178 	ldr	r3, [fp, #-376]	@ 0xfffffe88
400098ec:	e50b3038 	str	r3, [fp, #-56]	@ 0xffffffc8
400098f0:	e51b2038 	ldr	r2, [fp, #-56]	@ 0xffffffc8
400098f4:	e51b302c 	ldr	r3, [fp, #-44]	@ 0xffffffd4
400098f8:	e0823003 	add	r3, r2, r3
400098fc:	e50b303c 	str	r3, [fp, #-60]	@ 0xffffffc4
40009900:	e51b2038 	ldr	r2, [fp, #-56]	@ 0xffffffc8
40009904:	e51b3030 	ldr	r3, [fp, #-48]	@ 0xffffffd0
40009908:	e0823003 	add	r3, r2, r3
4000990c:	e50b3040 	str	r3, [fp, #-64]	@ 0xffffffc0
40009910:	e51b2038 	ldr	r2, [fp, #-56]	@ 0xffffffc8
40009914:	e51b3034 	ldr	r3, [fp, #-52]	@ 0xffffffcc
40009918:	e0823003 	add	r3, r2, r3
4000991c:	e50b3044 	str	r3, [fp, #-68]	@ 0xffffffbc
40009920:	e51b2040 	ldr	r2, [fp, #-64]	@ 0xffffffc0
40009924:	e51b3038 	ldr	r3, [fp, #-56]	@ 0xffffffc8
40009928:	e1520003 	cmp	r2, r3
4000992c:	3a000003 	bcc	40009940 <dtb_parse+0xec>
40009930:	e51b2040 	ldr	r2, [fp, #-64]	@ 0xffffffc0
40009934:	e51b303c 	ldr	r3, [fp, #-60]	@ 0xffffffc4
40009938:	e1520003 	cmp	r2, r3
4000993c:	3a000000 	bcc	40009944 <dtb_parse+0xf0>
40009940:	ebffff47 	bl	40009664 <halt>
40009944:	e51b2044 	ldr	r2, [fp, #-68]	@ 0xffffffbc
40009948:	e51b3038 	ldr	r3, [fp, #-56]	@ 0xffffffc8
4000994c:	e1520003 	cmp	r2, r3
40009950:	3a000003 	bcc	40009964 <dtb_parse+0x110>
40009954:	e51b2044 	ldr	r2, [fp, #-68]	@ 0xffffffbc
40009958:	e51b303c 	ldr	r3, [fp, #-60]	@ 0xffffffc4
4000995c:	e1520003 	cmp	r2, r3
40009960:	3a000000 	bcc	40009968 <dtb_parse+0x114>
40009964:	ebffff3e 	bl	40009664 <halt>
40009968:	e51b3040 	ldr	r3, [fp, #-64]	@ 0xffffffc0
4000996c:	e50b3008 	str	r3, [fp, #-8]
40009970:	e3a03000 	mov	r3, #0
40009974:	e50b300c 	str	r3, [fp, #-12]
40009978:	e3a03000 	mov	r3, #0
4000997c:	e50b3010 	str	r3, [fp, #-16]
40009980:	e51b3008 	ldr	r3, [fp, #-8]
40009984:	e2833004 	add	r3, r3, #4
40009988:	e51b203c 	ldr	r2, [fp, #-60]	@ 0xffffffc4
4000998c:	e1520003 	cmp	r2, r3
40009990:	2a000000 	bcs	40009998 <dtb_parse+0x144>
40009994:	ebffff32 	bl	40009664 <halt>
40009998:	e51b0008 	ldr	r0, [fp, #-8]
4000999c:	ebffff99 	bl	40009808 <read_be32>
400099a0:	e50b0048 	str	r0, [fp, #-72]	@ 0xffffffb8
400099a4:	e51b3008 	ldr	r3, [fp, #-8]
400099a8:	e2833004 	add	r3, r3, #4
400099ac:	e50b3008 	str	r3, [fp, #-8]
400099b0:	e51b3048 	ldr	r3, [fp, #-72]	@ 0xffffffb8
400099b4:	e3530009 	cmp	r3, #9
400099b8:	0a0000f1 	beq	40009d84 <dtb_parse+0x530>
400099bc:	e51b3048 	ldr	r3, [fp, #-72]	@ 0xffffffb8
400099c0:	e3530009 	cmp	r3, #9
400099c4:	8a0000f0 	bhi	40009d8c <dtb_parse+0x538>
400099c8:	e51b3048 	ldr	r3, [fp, #-72]	@ 0xffffffb8
400099cc:	e3530004 	cmp	r3, #4
400099d0:	0a0000ef 	beq	40009d94 <dtb_parse+0x540>
400099d4:	e51b3048 	ldr	r3, [fp, #-72]	@ 0xffffffb8
400099d8:	e3530004 	cmp	r3, #4
400099dc:	8a0000ea 	bhi	40009d8c <dtb_parse+0x538>
400099e0:	e51b3048 	ldr	r3, [fp, #-72]	@ 0xffffffb8
400099e4:	e3530003 	cmp	r3, #3
400099e8:	0a000067 	beq	40009b8c <dtb_parse+0x338>
400099ec:	e51b3048 	ldr	r3, [fp, #-72]	@ 0xffffffb8
400099f0:	e3530003 	cmp	r3, #3
400099f4:	8a0000e4 	bhi	40009d8c <dtb_parse+0x538>
400099f8:	e51b3048 	ldr	r3, [fp, #-72]	@ 0xffffffb8
400099fc:	e3530001 	cmp	r3, #1
40009a00:	0a000003 	beq	40009a14 <dtb_parse+0x1c0>
40009a04:	e51b3048 	ldr	r3, [fp, #-72]	@ 0xffffffb8
40009a08:	e3530002 	cmp	r3, #2
40009a0c:	0a0000d4 	beq	40009d64 <dtb_parse+0x510>
40009a10:	ea0000dd 	b	40009d8c <dtb_parse+0x538>
40009a14:	e51b2008 	ldr	r2, [fp, #-8]
40009a18:	e51b303c 	ldr	r3, [fp, #-60]	@ 0xffffffc4
40009a1c:	e1520003 	cmp	r2, r3
40009a20:	3a000000 	bcc	40009a28 <dtb_parse+0x1d4>
40009a24:	ebffff0e 	bl	40009664 <halt>
40009a28:	e51b3008 	ldr	r3, [fp, #-8]
40009a2c:	e50b3068 	str	r3, [fp, #-104]	@ 0xffffff98
40009a30:	e51b3008 	ldr	r3, [fp, #-8]
40009a34:	e50b3014 	str	r3, [fp, #-20]	@ 0xffffffec
40009a38:	e3a03000 	mov	r3, #0
40009a3c:	e54b3015 	strb	r3, [fp, #-21]	@ 0xffffffeb
40009a40:	ea000009 	b	40009a6c <dtb_parse+0x218>
40009a44:	e51b3014 	ldr	r3, [fp, #-20]	@ 0xffffffec
40009a48:	e5d33000 	ldrb	r3, [r3]
40009a4c:	e3530000 	cmp	r3, #0
40009a50:	1a000002 	bne	40009a60 <dtb_parse+0x20c>
40009a54:	e3a03001 	mov	r3, #1
40009a58:	e54b3015 	strb	r3, [fp, #-21]	@ 0xffffffeb
40009a5c:	ea000006 	b	40009a7c <dtb_parse+0x228>
40009a60:	e51b3014 	ldr	r3, [fp, #-20]	@ 0xffffffec
40009a64:	e2833001 	add	r3, r3, #1
40009a68:	e50b3014 	str	r3, [fp, #-20]	@ 0xffffffec
40009a6c:	e51b2014 	ldr	r2, [fp, #-20]	@ 0xffffffec
40009a70:	e51b303c 	ldr	r3, [fp, #-60]	@ 0xffffffc4
40009a74:	e1520003 	cmp	r2, r3
40009a78:	3afffff1 	bcc	40009a44 <dtb_parse+0x1f0>
40009a7c:	e55b3015 	ldrb	r3, [fp, #-21]	@ 0xffffffeb
40009a80:	e2233001 	eor	r3, r3, #1
40009a84:	e6ef3073 	uxtb	r3, r3
40009a88:	e3530000 	cmp	r3, #0
40009a8c:	0a000000 	beq	40009a94 <dtb_parse+0x240>
40009a90:	ebfffef3 	bl	40009664 <halt>
40009a94:	e51b2014 	ldr	r2, [fp, #-20]	@ 0xffffffec
40009a98:	e51b3008 	ldr	r3, [fp, #-8]
40009a9c:	e0423003 	sub	r3, r2, r3
40009aa0:	e2833001 	add	r3, r3, #1
40009aa4:	e50b306c 	str	r3, [fp, #-108]	@ 0xffffff94
40009aa8:	e51b2008 	ldr	r2, [fp, #-8]
40009aac:	e51b306c 	ldr	r3, [fp, #-108]	@ 0xffffff94
40009ab0:	e0823003 	add	r3, r2, r3
40009ab4:	e50b3008 	str	r3, [fp, #-8]
40009ab8:	ea000002 	b	40009ac8 <dtb_parse+0x274>
40009abc:	e51b3008 	ldr	r3, [fp, #-8]
40009ac0:	e2833001 	add	r3, r3, #1
40009ac4:	e50b3008 	str	r3, [fp, #-8]
40009ac8:	e51b3008 	ldr	r3, [fp, #-8]
40009acc:	e2033003 	and	r3, r3, #3
40009ad0:	e3530000 	cmp	r3, #0
40009ad4:	1afffff8 	bne	40009abc <dtb_parse+0x268>
40009ad8:	ebfffeef 	bl	4000969c <dtb_new_node>
40009adc:	e50b0070 	str	r0, [fp, #-112]	@ 0xffffff90
40009ae0:	e51b3070 	ldr	r3, [fp, #-112]	@ 0xffffff90
40009ae4:	e51b2068 	ldr	r2, [fp, #-104]	@ 0xffffff98
40009ae8:	e5832000 	str	r2, [r3]
40009aec:	e51b300c 	ldr	r3, [fp, #-12]
40009af0:	e3530000 	cmp	r3, #0
40009af4:	1a000002 	bne	40009b04 <dtb_parse+0x2b0>
40009af8:	e51b3070 	ldr	r3, [fp, #-112]	@ 0xffffff90
40009afc:	e50b3010 	str	r3, [fp, #-16]
40009b00:	ea000014 	b	40009b58 <dtb_parse+0x304>
40009b04:	e51b300c 	ldr	r3, [fp, #-12]
40009b08:	e2433001 	sub	r3, r3, #1
40009b0c:	e1a03103 	lsl	r3, r3, #2
40009b10:	e2433004 	sub	r3, r3, #4
40009b14:	e083300b 	add	r3, r3, fp
40009b18:	e5133170 	ldr	r3, [r3, #-368]	@ 0xfffffe90
40009b1c:	e50b3074 	str	r3, [fp, #-116]	@ 0xffffff8c
40009b20:	e51b3074 	ldr	r3, [fp, #-116]	@ 0xffffff8c
40009b24:	e5933208 	ldr	r3, [r3, #520]	@ 0x208
40009b28:	e353001f 	cmp	r3, #31
40009b2c:	9a000000 	bls	40009b34 <dtb_parse+0x2e0>
40009b30:	ebfffecb 	bl	40009664 <halt>
40009b34:	e51b3074 	ldr	r3, [fp, #-116]	@ 0xffffff8c
40009b38:	e5933208 	ldr	r3, [r3, #520]	@ 0x208
40009b3c:	e2831001 	add	r1, r3, #1
40009b40:	e51b2074 	ldr	r2, [fp, #-116]	@ 0xffffff8c
40009b44:	e5821208 	str	r1, [r2, #520]	@ 0x208
40009b48:	e51b2074 	ldr	r2, [fp, #-116]	@ 0xffffff8c
40009b4c:	e2833062 	add	r3, r3, #98	@ 0x62
40009b50:	e51b1070 	ldr	r1, [fp, #-112]	@ 0xffffff90
40009b54:	e7821103 	str	r1, [r2, r3, lsl #2]
40009b58:	e51b300c 	ldr	r3, [fp, #-12]
40009b5c:	e353003f 	cmp	r3, #63	@ 0x3f
40009b60:	9a000000 	bls	40009b68 <dtb_parse+0x314>
40009b64:	ebfffebe 	bl	40009664 <halt>
40009b68:	e51b300c 	ldr	r3, [fp, #-12]
40009b6c:	e2832001 	add	r2, r3, #1
40009b70:	e50b200c 	str	r2, [fp, #-12]
40009b74:	e1a03103 	lsl	r3, r3, #2
40009b78:	e2433004 	sub	r3, r3, #4
40009b7c:	e083300b 	add	r3, r3, fp
40009b80:	e51b2070 	ldr	r2, [fp, #-112]	@ 0xffffff90
40009b84:	e5032170 	str	r2, [r3, #-368]	@ 0xfffffe90
40009b88:	ea000082 	b	40009d98 <dtb_parse+0x544>
40009b8c:	e51b3008 	ldr	r3, [fp, #-8]
40009b90:	e2833008 	add	r3, r3, #8
40009b94:	e51b203c 	ldr	r2, [fp, #-60]	@ 0xffffffc4
40009b98:	e1520003 	cmp	r2, r3
40009b9c:	2a000000 	bcs	40009ba4 <dtb_parse+0x350>
40009ba0:	ebfffeaf 	bl	40009664 <halt>
40009ba4:	e51b0008 	ldr	r0, [fp, #-8]
40009ba8:	ebffff16 	bl	40009808 <read_be32>
40009bac:	e50b004c 	str	r0, [fp, #-76]	@ 0xffffffb4
40009bb0:	e51b3008 	ldr	r3, [fp, #-8]
40009bb4:	e2833004 	add	r3, r3, #4
40009bb8:	e50b3008 	str	r3, [fp, #-8]
40009bbc:	e51b0008 	ldr	r0, [fp, #-8]
40009bc0:	ebffff10 	bl	40009808 <read_be32>
40009bc4:	e50b0050 	str	r0, [fp, #-80]	@ 0xffffffb0
40009bc8:	e51b3008 	ldr	r3, [fp, #-8]
40009bcc:	e2833004 	add	r3, r3, #4
40009bd0:	e50b3008 	str	r3, [fp, #-8]
40009bd4:	e51b2044 	ldr	r2, [fp, #-68]	@ 0xffffffbc
40009bd8:	e51b3050 	ldr	r3, [fp, #-80]	@ 0xffffffb0
40009bdc:	e0823003 	add	r3, r2, r3
40009be0:	e50b3054 	str	r3, [fp, #-84]	@ 0xffffffac
40009be4:	e51b2054 	ldr	r2, [fp, #-84]	@ 0xffffffac
40009be8:	e51b3038 	ldr	r3, [fp, #-56]	@ 0xffffffc8
40009bec:	e1520003 	cmp	r2, r3
40009bf0:	3a000003 	bcc	40009c04 <dtb_parse+0x3b0>
40009bf4:	e51b2054 	ldr	r2, [fp, #-84]	@ 0xffffffac
40009bf8:	e51b303c 	ldr	r3, [fp, #-60]	@ 0xffffffc4
40009bfc:	e1520003 	cmp	r2, r3
40009c00:	3a000000 	bcc	40009c08 <dtb_parse+0x3b4>
40009c04:	ebfffe96 	bl	40009664 <halt>
40009c08:	e51b3054 	ldr	r3, [fp, #-84]	@ 0xffffffac
40009c0c:	e50b301c 	str	r3, [fp, #-28]	@ 0xffffffe4
40009c10:	e3a03000 	mov	r3, #0
40009c14:	e54b301d 	strb	r3, [fp, #-29]	@ 0xffffffe3
40009c18:	ea000009 	b	40009c44 <dtb_parse+0x3f0>
40009c1c:	e51b301c 	ldr	r3, [fp, #-28]	@ 0xffffffe4
40009c20:	e5d33000 	ldrb	r3, [r3]
40009c24:	e3530000 	cmp	r3, #0
40009c28:	1a000002 	bne	40009c38 <dtb_parse+0x3e4>
40009c2c:	e3a03001 	mov	r3, #1
40009c30:	e54b301d 	strb	r3, [fp, #-29]	@ 0xffffffe3
40009c34:	ea000006 	b	40009c54 <dtb_parse+0x400>
40009c38:	e51b301c 	ldr	r3, [fp, #-28]	@ 0xffffffe4
40009c3c:	e2833001 	add	r3, r3, #1
40009c40:	e50b301c 	str	r3, [fp, #-28]	@ 0xffffffe4
40009c44:	e51b201c 	ldr	r2, [fp, #-28]	@ 0xffffffe4
40009c48:	e51b303c 	ldr	r3, [fp, #-60]	@ 0xffffffc4
40009c4c:	e1520003 	cmp	r2, r3
40009c50:	3afffff1 	bcc	40009c1c <dtb_parse+0x3c8>
40009c54:	e55b301d 	ldrb	r3, [fp, #-29]	@ 0xffffffe3
40009c58:	e2233001 	eor	r3, r3, #1
40009c5c:	e6ef3073 	uxtb	r3, r3
40009c60:	e3530000 	cmp	r3, #0
40009c64:	0a000000 	beq	40009c6c <dtb_parse+0x418>
40009c68:	ebfffe7d 	bl	40009664 <halt>
40009c6c:	e51b3054 	ldr	r3, [fp, #-84]	@ 0xffffffac
40009c70:	e50b3058 	str	r3, [fp, #-88]	@ 0xffffffa8
40009c74:	e51b2008 	ldr	r2, [fp, #-8]
40009c78:	e51b304c 	ldr	r3, [fp, #-76]	@ 0xffffffb4
40009c7c:	e0823003 	add	r3, r2, r3
40009c80:	e51b203c 	ldr	r2, [fp, #-60]	@ 0xffffffc4
40009c84:	e1520003 	cmp	r2, r3
40009c88:	2a000000 	bcs	40009c90 <dtb_parse+0x43c>
40009c8c:	ebfffe74 	bl	40009664 <halt>
40009c90:	e51b3008 	ldr	r3, [fp, #-8]
40009c94:	e50b305c 	str	r3, [fp, #-92]	@ 0xffffffa4
40009c98:	e51b300c 	ldr	r3, [fp, #-12]
40009c9c:	e3530000 	cmp	r3, #0
40009ca0:	1a000000 	bne	40009ca8 <dtb_parse+0x454>
40009ca4:	ebfffe6e 	bl	40009664 <halt>
40009ca8:	e51b300c 	ldr	r3, [fp, #-12]
40009cac:	e2433001 	sub	r3, r3, #1
40009cb0:	e1a03103 	lsl	r3, r3, #2
40009cb4:	e2433004 	sub	r3, r3, #4
40009cb8:	e083300b 	add	r3, r3, fp
40009cbc:	e5133170 	ldr	r3, [r3, #-368]	@ 0xfffffe90
40009cc0:	e50b3060 	str	r3, [fp, #-96]	@ 0xffffffa0
40009cc4:	e51b3060 	ldr	r3, [fp, #-96]	@ 0xffffffa0
40009cc8:	e5933184 	ldr	r3, [r3, #388]	@ 0x184
40009ccc:	e353001f 	cmp	r3, #31
40009cd0:	9a000000 	bls	40009cd8 <dtb_parse+0x484>
40009cd4:	ebfffe62 	bl	40009664 <halt>
40009cd8:	e51b3060 	ldr	r3, [fp, #-96]	@ 0xffffffa0
40009cdc:	e5932184 	ldr	r2, [r3, #388]	@ 0x184
40009ce0:	e2821001 	add	r1, r2, #1
40009ce4:	e51b3060 	ldr	r3, [fp, #-96]	@ 0xffffffa0
40009ce8:	e5831184 	str	r1, [r3, #388]	@ 0x184
40009cec:	e1a03002 	mov	r3, r2
40009cf0:	e1a03083 	lsl	r3, r3, #1
40009cf4:	e0833002 	add	r3, r3, r2
40009cf8:	e1a03103 	lsl	r3, r3, #2
40009cfc:	e51b2060 	ldr	r2, [fp, #-96]	@ 0xffffffa0
40009d00:	e0823003 	add	r3, r2, r3
40009d04:	e2833004 	add	r3, r3, #4
40009d08:	e50b3064 	str	r3, [fp, #-100]	@ 0xffffff9c
40009d0c:	e51b3064 	ldr	r3, [fp, #-100]	@ 0xffffff9c
40009d10:	e51b2058 	ldr	r2, [fp, #-88]	@ 0xffffffa8
40009d14:	e5832000 	str	r2, [r3]
40009d18:	e51b3064 	ldr	r3, [fp, #-100]	@ 0xffffff9c
40009d1c:	e51b205c 	ldr	r2, [fp, #-92]	@ 0xffffffa4
40009d20:	e5832004 	str	r2, [r3, #4]
40009d24:	e51b3064 	ldr	r3, [fp, #-100]	@ 0xffffff9c
40009d28:	e51b204c 	ldr	r2, [fp, #-76]	@ 0xffffffb4
40009d2c:	e5832008 	str	r2, [r3, #8]
40009d30:	e51b2008 	ldr	r2, [fp, #-8]
40009d34:	e51b304c 	ldr	r3, [fp, #-76]	@ 0xffffffb4
40009d38:	e0823003 	add	r3, r2, r3
40009d3c:	e50b3008 	str	r3, [fp, #-8]
40009d40:	ea000002 	b	40009d50 <dtb_parse+0x4fc>
40009d44:	e51b3008 	ldr	r3, [fp, #-8]
40009d48:	e2833001 	add	r3, r3, #1
40009d4c:	e50b3008 	str	r3, [fp, #-8]
40009d50:	e51b3008 	ldr	r3, [fp, #-8]
40009d54:	e2033003 	and	r3, r3, #3
40009d58:	e3530000 	cmp	r3, #0
40009d5c:	1afffff8 	bne	40009d44 <dtb_parse+0x4f0>
40009d60:	ea00000c 	b	40009d98 <dtb_parse+0x544>
40009d64:	e51b300c 	ldr	r3, [fp, #-12]
40009d68:	e3530000 	cmp	r3, #0
40009d6c:	1a000000 	bne	40009d74 <dtb_parse+0x520>
40009d70:	ebfffe3b 	bl	40009664 <halt>
40009d74:	e51b300c 	ldr	r3, [fp, #-12]
40009d78:	e2433001 	sub	r3, r3, #1
40009d7c:	e50b300c 	str	r3, [fp, #-12]
40009d80:	ea000004 	b	40009d98 <dtb_parse+0x544>
40009d84:	e51b3010 	ldr	r3, [fp, #-16]
40009d88:	ea000003 	b	40009d9c <dtb_parse+0x548>
40009d8c:	ebfffe34 	bl	40009664 <halt>
40009d90:	eafffefa 	b	40009980 <dtb_parse+0x12c>
40009d94:	e320f000 	nop	{0}
40009d98:	eafffef8 	b	40009980 <dtb_parse+0x12c>
40009d9c:	e1a00003 	mov	r0, r3
40009da0:	e24bd004 	sub	sp, fp, #4
40009da4:	e59db000 	ldr	fp, [sp]
40009da8:	e28dd004 	add	sp, sp, #4
40009dac:	e49df004 	pop	{pc}		@ (ldr pc, [sp], #4)

40009db0 <dtb_find_node_rec>:
40009db0:	e52db008 	str	fp, [sp, #-8]!
40009db4:	e58de004 	str	lr, [sp, #4]
40009db8:	e28db004 	add	fp, sp, #4
40009dbc:	e24dd020 	sub	sp, sp, #32
40009dc0:	e50b0020 	str	r0, [fp, #-32]	@ 0xffffffe0
40009dc4:	e50b1024 	str	r1, [fp, #-36]	@ 0xffffffdc
40009dc8:	e51b3024 	ldr	r3, [fp, #-36]	@ 0xffffffdc
40009dcc:	e5d33000 	ldrb	r3, [r3]
40009dd0:	e3530000 	cmp	r3, #0
40009dd4:	1a000001 	bne	40009de0 <dtb_find_node_rec+0x30>
40009dd8:	e51b3020 	ldr	r3, [fp, #-32]	@ 0xffffffe0
40009ddc:	ea00004c 	b	40009f14 <dtb_find_node_rec+0x164>
40009de0:	e51b3024 	ldr	r3, [fp, #-36]	@ 0xffffffdc
40009de4:	e5d33000 	ldrb	r3, [r3]
40009de8:	e353002f 	cmp	r3, #47	@ 0x2f
40009dec:	1a000002 	bne	40009dfc <dtb_find_node_rec+0x4c>
40009df0:	e51b3024 	ldr	r3, [fp, #-36]	@ 0xffffffdc
40009df4:	e2833001 	add	r3, r3, #1
40009df8:	e50b3024 	str	r3, [fp, #-36]	@ 0xffffffdc
40009dfc:	e3a0102f 	mov	r1, #47	@ 0x2f
40009e00:	e51b0024 	ldr	r0, [fp, #-36]	@ 0xffffffdc
40009e04:	ebfffc02 	bl	40008e14 <strchr>
40009e08:	e50b0010 	str	r0, [fp, #-16]
40009e0c:	e51b3010 	ldr	r3, [fp, #-16]
40009e10:	e3530000 	cmp	r3, #0
40009e14:	0a000004 	beq	40009e2c <dtb_find_node_rec+0x7c>
40009e18:	e51b2010 	ldr	r2, [fp, #-16]
40009e1c:	e51b3024 	ldr	r3, [fp, #-36]	@ 0xffffffdc
40009e20:	e0423003 	sub	r3, r2, r3
40009e24:	e50b3008 	str	r3, [fp, #-8]
40009e28:	ea000002 	b	40009e38 <dtb_find_node_rec+0x88>
40009e2c:	e51b0024 	ldr	r0, [fp, #-36]	@ 0xffffffdc
40009e30:	ebfffb09 	bl	40008a5c <strlen>
40009e34:	e50b0008 	str	r0, [fp, #-8]
40009e38:	e3a03000 	mov	r3, #0
40009e3c:	e50b300c 	str	r3, [fp, #-12]
40009e40:	ea00002d 	b	40009efc <dtb_find_node_rec+0x14c>
40009e44:	e51b3020 	ldr	r3, [fp, #-32]	@ 0xffffffe0
40009e48:	e51b200c 	ldr	r2, [fp, #-12]
40009e4c:	e2822062 	add	r2, r2, #98	@ 0x62
40009e50:	e7933102 	ldr	r3, [r3, r2, lsl #2]
40009e54:	e50b3014 	str	r3, [fp, #-20]	@ 0xffffffec
40009e58:	e51b3014 	ldr	r3, [fp, #-20]	@ 0xffffffec
40009e5c:	e5933000 	ldr	r3, [r3]
40009e60:	e50b3018 	str	r3, [fp, #-24]	@ 0xffffffe8
40009e64:	e51b3018 	ldr	r3, [fp, #-24]	@ 0xffffffe8
40009e68:	e3530000 	cmp	r3, #0
40009e6c:	0a00001e 	beq	40009eec <dtb_find_node_rec+0x13c>
40009e70:	e51b2008 	ldr	r2, [fp, #-8]
40009e74:	e51b1024 	ldr	r1, [fp, #-36]	@ 0xffffffdc
40009e78:	e51b0018 	ldr	r0, [fp, #-24]	@ 0xffffffe8
40009e7c:	ebfffb6a 	bl	40008c2c <strncmp>
40009e80:	e1a03000 	mov	r3, r0
40009e84:	e3530000 	cmp	r3, #0
40009e88:	1a000018 	bne	40009ef0 <dtb_find_node_rec+0x140>
40009e8c:	e51b2018 	ldr	r2, [fp, #-24]	@ 0xffffffe8
40009e90:	e51b3008 	ldr	r3, [fp, #-8]
40009e94:	e0823003 	add	r3, r2, r3
40009e98:	e5d33000 	ldrb	r3, [r3]
40009e9c:	e3530000 	cmp	r3, #0
40009ea0:	0a000005 	beq	40009ebc <dtb_find_node_rec+0x10c>
40009ea4:	e51b2018 	ldr	r2, [fp, #-24]	@ 0xffffffe8
40009ea8:	e51b3008 	ldr	r3, [fp, #-8]
40009eac:	e0823003 	add	r3, r2, r3
40009eb0:	e5d33000 	ldrb	r3, [r3]
40009eb4:	e3530040 	cmp	r3, #64	@ 0x40
40009eb8:	1a00000c 	bne	40009ef0 <dtb_find_node_rec+0x140>
40009ebc:	e51b3010 	ldr	r3, [fp, #-16]
40009ec0:	e3530000 	cmp	r3, #0
40009ec4:	1a000001 	bne	40009ed0 <dtb_find_node_rec+0x120>
40009ec8:	e51b3014 	ldr	r3, [fp, #-20]	@ 0xffffffec
40009ecc:	ea000010 	b	40009f14 <dtb_find_node_rec+0x164>
40009ed0:	e51b3010 	ldr	r3, [fp, #-16]
40009ed4:	e2833001 	add	r3, r3, #1
40009ed8:	e1a01003 	mov	r1, r3
40009edc:	e51b0014 	ldr	r0, [fp, #-20]	@ 0xffffffec
40009ee0:	ebffffb2 	bl	40009db0 <dtb_find_node_rec>
40009ee4:	e1a03000 	mov	r3, r0
40009ee8:	ea000009 	b	40009f14 <dtb_find_node_rec+0x164>
40009eec:	e320f000 	nop	{0}
40009ef0:	e51b300c 	ldr	r3, [fp, #-12]
40009ef4:	e2833001 	add	r3, r3, #1
40009ef8:	e50b300c 	str	r3, [fp, #-12]
40009efc:	e51b3020 	ldr	r3, [fp, #-32]	@ 0xffffffe0
40009f00:	e5933208 	ldr	r3, [r3, #520]	@ 0x208
40009f04:	e51b200c 	ldr	r2, [fp, #-12]
40009f08:	e1520003 	cmp	r2, r3
40009f0c:	3affffcc 	bcc	40009e44 <dtb_find_node_rec+0x94>
40009f10:	e3a03000 	mov	r3, #0
40009f14:	e1a00003 	mov	r0, r3
40009f18:	e24bd004 	sub	sp, fp, #4
40009f1c:	e59db000 	ldr	fp, [sp]
40009f20:	e28dd004 	add	sp, sp, #4
40009f24:	e49df004 	pop	{pc}		@ (ldr pc, [sp], #4)

40009f28 <dtb_find_node>:
40009f28:	e52db008 	str	fp, [sp, #-8]!
40009f2c:	e58de004 	str	lr, [sp, #4]
40009f30:	e28db004 	add	fp, sp, #4
40009f34:	e24dd008 	sub	sp, sp, #8
40009f38:	e50b0008 	str	r0, [fp, #-8]
40009f3c:	e50b100c 	str	r1, [fp, #-12]
40009f40:	e51b3008 	ldr	r3, [fp, #-8]
40009f44:	e3530000 	cmp	r3, #0
40009f48:	0a000002 	beq	40009f58 <dtb_find_node+0x30>
40009f4c:	e51b300c 	ldr	r3, [fp, #-12]
40009f50:	e3530000 	cmp	r3, #0
40009f54:	1a000001 	bne	40009f60 <dtb_find_node+0x38>
40009f58:	e3a03000 	mov	r3, #0
40009f5c:	ea00000c 	b	40009f94 <dtb_find_node+0x6c>
40009f60:	e30b1140 	movw	r1, #45376	@ 0xb140
40009f64:	e3441000 	movt	r1, #16384	@ 0x4000
40009f68:	e51b000c 	ldr	r0, [fp, #-12]
40009f6c:	ebfffb06 	bl	40008b8c <strcmp>
40009f70:	e1a03000 	mov	r3, r0
40009f74:	e3530000 	cmp	r3, #0
40009f78:	1a000001 	bne	40009f84 <dtb_find_node+0x5c>
40009f7c:	e51b3008 	ldr	r3, [fp, #-8]
40009f80:	ea000003 	b	40009f94 <dtb_find_node+0x6c>
40009f84:	e51b100c 	ldr	r1, [fp, #-12]
40009f88:	e51b0008 	ldr	r0, [fp, #-8]
40009f8c:	ebffff87 	bl	40009db0 <dtb_find_node_rec>
40009f90:	e1a03000 	mov	r3, r0
40009f94:	e1a00003 	mov	r0, r3
40009f98:	e24bd004 	sub	sp, fp, #4
40009f9c:	e59db000 	ldr	fp, [sp]
40009fa0:	e28dd004 	add	sp, sp, #4
40009fa4:	e49df004 	pop	{pc}		@ (ldr pc, [sp], #4)

40009fa8 <dtb_get_property>:
40009fa8:	e52db008 	str	fp, [sp, #-8]!
40009fac:	e58de004 	str	lr, [sp, #4]
40009fb0:	e28db004 	add	fp, sp, #4
40009fb4:	e24dd018 	sub	sp, sp, #24
40009fb8:	e50b0010 	str	r0, [fp, #-16]
40009fbc:	e50b1014 	str	r1, [fp, #-20]	@ 0xffffffec
40009fc0:	e50b2018 	str	r2, [fp, #-24]	@ 0xffffffe8
40009fc4:	e51b1014 	ldr	r1, [fp, #-20]	@ 0xffffffec
40009fc8:	e51b0010 	ldr	r0, [fp, #-16]
40009fcc:	ebffffd5 	bl	40009f28 <dtb_find_node>
40009fd0:	e50b000c 	str	r0, [fp, #-12]
40009fd4:	e51b300c 	ldr	r3, [fp, #-12]
40009fd8:	e3530000 	cmp	r3, #0
40009fdc:	1a000001 	bne	40009fe8 <dtb_get_property+0x40>
40009fe0:	e3a03000 	mov	r3, #0
40009fe4:	ea000024 	b	4000a07c <dtb_get_property+0xd4>
40009fe8:	e3a03000 	mov	r3, #0
40009fec:	e50b3008 	str	r3, [fp, #-8]
40009ff0:	ea00001b 	b	4000a064 <dtb_get_property+0xbc>
40009ff4:	e51b100c 	ldr	r1, [fp, #-12]
40009ff8:	e51b2008 	ldr	r2, [fp, #-8]
40009ffc:	e1a03002 	mov	r3, r2
4000a000:	e1a03083 	lsl	r3, r3, #1
4000a004:	e0833002 	add	r3, r3, r2
4000a008:	e1a03103 	lsl	r3, r3, #2
4000a00c:	e0813003 	add	r3, r1, r3
4000a010:	e2833004 	add	r3, r3, #4
4000a014:	e5933000 	ldr	r3, [r3]
4000a018:	e51b1018 	ldr	r1, [fp, #-24]	@ 0xffffffe8
4000a01c:	e1a00003 	mov	r0, r3
4000a020:	ebfffad9 	bl	40008b8c <strcmp>
4000a024:	e1a03000 	mov	r3, r0
4000a028:	e3530000 	cmp	r3, #0
4000a02c:	1a000009 	bne	4000a058 <dtb_get_property+0xb0>
4000a030:	e51b100c 	ldr	r1, [fp, #-12]
4000a034:	e51b2008 	ldr	r2, [fp, #-8]
4000a038:	e1a03002 	mov	r3, r2
4000a03c:	e1a03083 	lsl	r3, r3, #1
4000a040:	e0833002 	add	r3, r3, r2
4000a044:	e1a03103 	lsl	r3, r3, #2
4000a048:	e0813003 	add	r3, r1, r3
4000a04c:	e2833008 	add	r3, r3, #8
4000a050:	e5933000 	ldr	r3, [r3]
4000a054:	ea000008 	b	4000a07c <dtb_get_property+0xd4>
4000a058:	e51b3008 	ldr	r3, [fp, #-8]
4000a05c:	e2833001 	add	r3, r3, #1
4000a060:	e50b3008 	str	r3, [fp, #-8]
4000a064:	e51b300c 	ldr	r3, [fp, #-12]
4000a068:	e5933184 	ldr	r3, [r3, #388]	@ 0x184
4000a06c:	e51b2008 	ldr	r2, [fp, #-8]
4000a070:	e1520003 	cmp	r2, r3
4000a074:	3affffde 	bcc	40009ff4 <dtb_get_property+0x4c>
4000a078:	e3a03000 	mov	r3, #0
4000a07c:	e1a00003 	mov	r0, r3
4000a080:	e24bd004 	sub	sp, fp, #4
4000a084:	e59db000 	ldr	fp, [sp]
4000a088:	e28dd004 	add	sp, sp, #4
4000a08c:	e49df004 	pop	{pc}		@ (ldr pc, [sp], #4)

4000a090 <dtb_get_reg>:
4000a090:	e16d42f0 	strd	r4, [sp, #-32]!	@ 0xffffffe0
4000a094:	e1cd60f8 	strd	r6, [sp, #8]
4000a098:	e1cd81f0 	strd	r8, [sp, #16]
4000a09c:	e58db018 	str	fp, [sp, #24]
4000a0a0:	e58de01c 	str	lr, [sp, #28]
4000a0a4:	e28db01c 	add	fp, sp, #28
4000a0a8:	e24dd020 	sub	sp, sp, #32
4000a0ac:	e50b0038 	str	r0, [fp, #-56]	@ 0xffffffc8
4000a0b0:	e50b103c 	str	r1, [fp, #-60]	@ 0xffffffc4
4000a0b4:	e51b103c 	ldr	r1, [fp, #-60]	@ 0xffffffc4
4000a0b8:	e51b0038 	ldr	r0, [fp, #-56]	@ 0xffffffc8
4000a0bc:	ebffff99 	bl	40009f28 <dtb_find_node>
4000a0c0:	e50b0028 	str	r0, [fp, #-40]	@ 0xffffffd8
4000a0c4:	e51b3028 	ldr	r3, [fp, #-40]	@ 0xffffffd8
4000a0c8:	e3530000 	cmp	r3, #0
4000a0cc:	1a000002 	bne	4000a0dc <dtb_get_reg+0x4c>
4000a0d0:	e3a04000 	mov	r4, #0
4000a0d4:	e3a05000 	mov	r5, #0
4000a0d8:	ea000054 	b	4000a230 <dtb_get_reg+0x1a0>
4000a0dc:	e3a03000 	mov	r3, #0
4000a0e0:	e50b3020 	str	r3, [fp, #-32]	@ 0xffffffe0
4000a0e4:	e3a03000 	mov	r3, #0
4000a0e8:	e50b3024 	str	r3, [fp, #-36]	@ 0xffffffdc
4000a0ec:	ea00001c 	b	4000a164 <dtb_get_reg+0xd4>
4000a0f0:	e51b1028 	ldr	r1, [fp, #-40]	@ 0xffffffd8
4000a0f4:	e51b2024 	ldr	r2, [fp, #-36]	@ 0xffffffdc
4000a0f8:	e1a03002 	mov	r3, r2
4000a0fc:	e1a03083 	lsl	r3, r3, #1
4000a100:	e0833002 	add	r3, r3, r2
4000a104:	e1a03103 	lsl	r3, r3, #2
4000a108:	e0813003 	add	r3, r1, r3
4000a10c:	e2833004 	add	r3, r3, #4
4000a110:	e5933000 	ldr	r3, [r3]
4000a114:	e30b1144 	movw	r1, #45380	@ 0xb144
4000a118:	e3441000 	movt	r1, #16384	@ 0x4000
4000a11c:	e1a00003 	mov	r0, r3
4000a120:	ebfffa99 	bl	40008b8c <strcmp>
4000a124:	e1a03000 	mov	r3, r0
4000a128:	e3530000 	cmp	r3, #0
4000a12c:	1a000009 	bne	4000a158 <dtb_get_reg+0xc8>
4000a130:	e51b2024 	ldr	r2, [fp, #-36]	@ 0xffffffdc
4000a134:	e1a03002 	mov	r3, r2
4000a138:	e1a03083 	lsl	r3, r3, #1
4000a13c:	e0833002 	add	r3, r3, r2
4000a140:	e1a03103 	lsl	r3, r3, #2
4000a144:	e51b2028 	ldr	r2, [fp, #-40]	@ 0xffffffd8
4000a148:	e0823003 	add	r3, r2, r3
4000a14c:	e2833004 	add	r3, r3, #4
4000a150:	e50b3020 	str	r3, [fp, #-32]	@ 0xffffffe0
4000a154:	ea000007 	b	4000a178 <dtb_get_reg+0xe8>
4000a158:	e51b3024 	ldr	r3, [fp, #-36]	@ 0xffffffdc
4000a15c:	e2833001 	add	r3, r3, #1
4000a160:	e50b3024 	str	r3, [fp, #-36]	@ 0xffffffdc
4000a164:	e51b3028 	ldr	r3, [fp, #-40]	@ 0xffffffd8
4000a168:	e5933184 	ldr	r3, [r3, #388]	@ 0x184
4000a16c:	e51b2024 	ldr	r2, [fp, #-36]	@ 0xffffffdc
4000a170:	e1520003 	cmp	r2, r3
4000a174:	3affffdd 	bcc	4000a0f0 <dtb_get_reg+0x60>
4000a178:	e51b3020 	ldr	r3, [fp, #-32]	@ 0xffffffe0
4000a17c:	e3530000 	cmp	r3, #0
4000a180:	1a000002 	bne	4000a190 <dtb_get_reg+0x100>
4000a184:	e3a04000 	mov	r4, #0
4000a188:	e3a05000 	mov	r5, #0
4000a18c:	ea000027 	b	4000a230 <dtb_get_reg+0x1a0>
4000a190:	e51b3020 	ldr	r3, [fp, #-32]	@ 0xffffffe0
4000a194:	e5933008 	ldr	r3, [r3, #8]
4000a198:	e3530007 	cmp	r3, #7
4000a19c:	8a000002 	bhi	4000a1ac <dtb_get_reg+0x11c>
4000a1a0:	e3a04000 	mov	r4, #0
4000a1a4:	e3a05000 	mov	r5, #0
4000a1a8:	ea000020 	b	4000a230 <dtb_get_reg+0x1a0>
4000a1ac:	e51b3020 	ldr	r3, [fp, #-32]	@ 0xffffffe0
4000a1b0:	e5931004 	ldr	r1, [r3, #4]
4000a1b4:	e24b302c 	sub	r3, fp, #44	@ 0x2c
4000a1b8:	e3a02004 	mov	r2, #4
4000a1bc:	e1a00003 	mov	r0, r3
4000a1c0:	ebfff868 	bl	40008368 <memcpy>
4000a1c4:	e51b3020 	ldr	r3, [fp, #-32]	@ 0xffffffe0
4000a1c8:	e5933004 	ldr	r3, [r3, #4]
4000a1cc:	e2831004 	add	r1, r3, #4
4000a1d0:	e24b3030 	sub	r3, fp, #48	@ 0x30
4000a1d4:	e3a02004 	mov	r2, #4
4000a1d8:	e1a00003 	mov	r0, r3
4000a1dc:	ebfff861 	bl	40008368 <memcpy>
4000a1e0:	e51b302c 	ldr	r3, [fp, #-44]	@ 0xffffffd4
4000a1e4:	e6bf3f33 	rev	r3, r3
4000a1e8:	e50b302c 	str	r3, [fp, #-44]	@ 0xffffffd4
4000a1ec:	e51b3030 	ldr	r3, [fp, #-48]	@ 0xffffffd0
4000a1f0:	e6bf3f33 	rev	r3, r3
4000a1f4:	e50b3030 	str	r3, [fp, #-48]	@ 0xffffffd0
4000a1f8:	e51b302c 	ldr	r3, [fp, #-44]	@ 0xffffffd4
4000a1fc:	e3a02000 	mov	r2, #0
4000a200:	e1a08003 	mov	r8, r3
4000a204:	e1a09002 	mov	r9, r2
4000a208:	e3a02000 	mov	r2, #0
4000a20c:	e3a03000 	mov	r3, #0
4000a210:	e1a03008 	mov	r3, r8
4000a214:	e3a02000 	mov	r2, #0
4000a218:	e51b1030 	ldr	r1, [fp, #-48]	@ 0xffffffd0
4000a21c:	e3a00000 	mov	r0, #0
4000a220:	e1a06001 	mov	r6, r1
4000a224:	e1a07000 	mov	r7, r0
4000a228:	e1824006 	orr	r4, r2, r6
4000a22c:	e1835007 	orr	r5, r3, r7
4000a230:	e1a02004 	mov	r2, r4
4000a234:	e1a03005 	mov	r3, r5
4000a238:	e1a00002 	mov	r0, r2
4000a23c:	e1a01003 	mov	r1, r3
4000a240:	e24bd01c 	sub	sp, fp, #28
4000a244:	e1cd40d0 	ldrd	r4, [sp]
4000a248:	e1cd60d8 	ldrd	r6, [sp, #8]
4000a24c:	e1cd81d0 	ldrd	r8, [sp, #16]
4000a250:	e59db018 	ldr	fp, [sp, #24]
4000a254:	e28dd01c 	add	sp, sp, #28
4000a258:	e49df004 	pop	{pc}		@ (ldr pc, [sp], #4)

4000a25c <dtb_get_reg_addr>:
4000a25c:	e52db008 	str	fp, [sp, #-8]!
4000a260:	e58de004 	str	lr, [sp, #4]
4000a264:	e28db004 	add	fp, sp, #4
4000a268:	e24dd018 	sub	sp, sp, #24
4000a26c:	e50b0018 	str	r0, [fp, #-24]	@ 0xffffffe8
4000a270:	e50b101c 	str	r1, [fp, #-28]	@ 0xffffffe4
4000a274:	e51b101c 	ldr	r1, [fp, #-28]	@ 0xffffffe4
4000a278:	e51b0018 	ldr	r0, [fp, #-24]	@ 0xffffffe8
4000a27c:	ebffff29 	bl	40009f28 <dtb_find_node>
4000a280:	e50b000c 	str	r0, [fp, #-12]
4000a284:	e51b300c 	ldr	r3, [fp, #-12]
4000a288:	e3530000 	cmp	r3, #0
4000a28c:	1a000001 	bne	4000a298 <dtb_get_reg_addr+0x3c>
4000a290:	e3a03000 	mov	r3, #0
4000a294:	ea000039 	b	4000a380 <dtb_get_reg_addr+0x124>
4000a298:	e3a03000 	mov	r3, #0
4000a29c:	e50b3008 	str	r3, [fp, #-8]
4000a2a0:	ea000030 	b	4000a368 <dtb_get_reg_addr+0x10c>
4000a2a4:	e51b100c 	ldr	r1, [fp, #-12]
4000a2a8:	e51b2008 	ldr	r2, [fp, #-8]
4000a2ac:	e1a03002 	mov	r3, r2
4000a2b0:	e1a03083 	lsl	r3, r3, #1
4000a2b4:	e0833002 	add	r3, r3, r2
4000a2b8:	e1a03103 	lsl	r3, r3, #2
4000a2bc:	e0813003 	add	r3, r1, r3
4000a2c0:	e2833004 	add	r3, r3, #4
4000a2c4:	e5933000 	ldr	r3, [r3]
4000a2c8:	e30b1144 	movw	r1, #45380	@ 0xb144
4000a2cc:	e3441000 	movt	r1, #16384	@ 0x4000
4000a2d0:	e1a00003 	mov	r0, r3
4000a2d4:	ebfffa2c 	bl	40008b8c <strcmp>
4000a2d8:	e1a03000 	mov	r3, r0
4000a2dc:	e3530000 	cmp	r3, #0
4000a2e0:	1a00001d 	bne	4000a35c <dtb_get_reg_addr+0x100>
4000a2e4:	e51b100c 	ldr	r1, [fp, #-12]
4000a2e8:	e51b2008 	ldr	r2, [fp, #-8]
4000a2ec:	e1a03002 	mov	r3, r2
4000a2f0:	e1a03083 	lsl	r3, r3, #1
4000a2f4:	e0833002 	add	r3, r3, r2
4000a2f8:	e1a03103 	lsl	r3, r3, #2
4000a2fc:	e0813003 	add	r3, r1, r3
4000a300:	e2833008 	add	r3, r3, #8
4000a304:	e5933000 	ldr	r3, [r3]
4000a308:	e50b3010 	str	r3, [fp, #-16]
4000a30c:	e51b100c 	ldr	r1, [fp, #-12]
4000a310:	e51b2008 	ldr	r2, [fp, #-8]
4000a314:	e1a03002 	mov	r3, r2
4000a318:	e1a03083 	lsl	r3, r3, #1
4000a31c:	e0833002 	add	r3, r3, r2
4000a320:	e1a03103 	lsl	r3, r3, #2
4000a324:	e0813003 	add	r3, r1, r3
4000a328:	e283300c 	add	r3, r3, #12
4000a32c:	e5933000 	ldr	r3, [r3]
4000a330:	e3530003 	cmp	r3, #3
4000a334:	8a000001 	bhi	4000a340 <dtb_get_reg_addr+0xe4>
4000a338:	e3a03000 	mov	r3, #0
4000a33c:	ea00000f 	b	4000a380 <dtb_get_reg_addr+0x124>
4000a340:	e51b3010 	ldr	r3, [fp, #-16]
4000a344:	e5933000 	ldr	r3, [r3]
4000a348:	e50b3014 	str	r3, [fp, #-20]	@ 0xffffffec
4000a34c:	e51b0014 	ldr	r0, [fp, #-20]	@ 0xffffffec
4000a350:	ebfffcc7 	bl	40009674 <bswap32>
4000a354:	e1a03000 	mov	r3, r0
4000a358:	ea000008 	b	4000a380 <dtb_get_reg_addr+0x124>
4000a35c:	e51b3008 	ldr	r3, [fp, #-8]
4000a360:	e2833001 	add	r3, r3, #1
4000a364:	e50b3008 	str	r3, [fp, #-8]
4000a368:	e51b300c 	ldr	r3, [fp, #-12]
4000a36c:	e5933184 	ldr	r3, [r3, #388]	@ 0x184
4000a370:	e51b2008 	ldr	r2, [fp, #-8]
4000a374:	e1520003 	cmp	r2, r3
4000a378:	3affffc9 	bcc	4000a2a4 <dtb_get_reg_addr+0x48>
4000a37c:	e3a03000 	mov	r3, #0
4000a380:	e1a00003 	mov	r0, r3
4000a384:	e24bd004 	sub	sp, fp, #4
4000a388:	e59db000 	ldr	fp, [sp]
4000a38c:	e28dd004 	add	sp, sp, #4
4000a390:	e49df004 	pop	{pc}		@ (ldr pc, [sp], #4)

4000a394 <dtb_get_reg_size>:
4000a394:	e52db008 	str	fp, [sp, #-8]!
4000a398:	e58de004 	str	lr, [sp, #4]
4000a39c:	e28db004 	add	fp, sp, #4
4000a3a0:	e24dd018 	sub	sp, sp, #24
4000a3a4:	e50b0018 	str	r0, [fp, #-24]	@ 0xffffffe8
4000a3a8:	e50b101c 	str	r1, [fp, #-28]	@ 0xffffffe4
4000a3ac:	e51b101c 	ldr	r1, [fp, #-28]	@ 0xffffffe4
4000a3b0:	e51b0018 	ldr	r0, [fp, #-24]	@ 0xffffffe8
4000a3b4:	ebfffedb 	bl	40009f28 <dtb_find_node>
4000a3b8:	e50b000c 	str	r0, [fp, #-12]
4000a3bc:	e51b300c 	ldr	r3, [fp, #-12]
4000a3c0:	e3530000 	cmp	r3, #0
4000a3c4:	1a000001 	bne	4000a3d0 <dtb_get_reg_size+0x3c>
4000a3c8:	e3a03000 	mov	r3, #0
4000a3cc:	ea000039 	b	4000a4b8 <dtb_get_reg_size+0x124>
4000a3d0:	e3a03000 	mov	r3, #0
4000a3d4:	e50b3008 	str	r3, [fp, #-8]
4000a3d8:	ea000030 	b	4000a4a0 <dtb_get_reg_size+0x10c>
4000a3dc:	e51b100c 	ldr	r1, [fp, #-12]
4000a3e0:	e51b2008 	ldr	r2, [fp, #-8]
4000a3e4:	e1a03002 	mov	r3, r2
4000a3e8:	e1a03083 	lsl	r3, r3, #1
4000a3ec:	e0833002 	add	r3, r3, r2
4000a3f0:	e1a03103 	lsl	r3, r3, #2
4000a3f4:	e0813003 	add	r3, r1, r3
4000a3f8:	e2833004 	add	r3, r3, #4
4000a3fc:	e5933000 	ldr	r3, [r3]
4000a400:	e30b1144 	movw	r1, #45380	@ 0xb144
4000a404:	e3441000 	movt	r1, #16384	@ 0x4000
4000a408:	e1a00003 	mov	r0, r3
4000a40c:	ebfff9de 	bl	40008b8c <strcmp>
4000a410:	e1a03000 	mov	r3, r0
4000a414:	e3530000 	cmp	r3, #0
4000a418:	1a00001d 	bne	4000a494 <dtb_get_reg_size+0x100>
4000a41c:	e51b100c 	ldr	r1, [fp, #-12]
4000a420:	e51b2008 	ldr	r2, [fp, #-8]
4000a424:	e1a03002 	mov	r3, r2
4000a428:	e1a03083 	lsl	r3, r3, #1
4000a42c:	e0833002 	add	r3, r3, r2
4000a430:	e1a03103 	lsl	r3, r3, #2
4000a434:	e0813003 	add	r3, r1, r3
4000a438:	e2833008 	add	r3, r3, #8
4000a43c:	e5933000 	ldr	r3, [r3]
4000a440:	e50b3010 	str	r3, [fp, #-16]
4000a444:	e51b100c 	ldr	r1, [fp, #-12]
4000a448:	e51b2008 	ldr	r2, [fp, #-8]
4000a44c:	e1a03002 	mov	r3, r2
4000a450:	e1a03083 	lsl	r3, r3, #1
4000a454:	e0833002 	add	r3, r3, r2
4000a458:	e1a03103 	lsl	r3, r3, #2
4000a45c:	e0813003 	add	r3, r1, r3
4000a460:	e283300c 	add	r3, r3, #12
4000a464:	e5933000 	ldr	r3, [r3]
4000a468:	e3530007 	cmp	r3, #7
4000a46c:	8a000001 	bhi	4000a478 <dtb_get_reg_size+0xe4>
4000a470:	e3a03000 	mov	r3, #0
4000a474:	ea00000f 	b	4000a4b8 <dtb_get_reg_size+0x124>
4000a478:	e51b3010 	ldr	r3, [fp, #-16]
4000a47c:	e5933004 	ldr	r3, [r3, #4]
4000a480:	e50b3014 	str	r3, [fp, #-20]	@ 0xffffffec
4000a484:	e51b0014 	ldr	r0, [fp, #-20]	@ 0xffffffec
4000a488:	ebfffc79 	bl	40009674 <bswap32>
4000a48c:	e1a03000 	mov	r3, r0
4000a490:	ea000008 	b	4000a4b8 <dtb_get_reg_size+0x124>
4000a494:	e51b3008 	ldr	r3, [fp, #-8]
4000a498:	e2833001 	add	r3, r3, #1
4000a49c:	e50b3008 	str	r3, [fp, #-8]
4000a4a0:	e51b300c 	ldr	r3, [fp, #-12]
4000a4a4:	e5933184 	ldr	r3, [r3, #388]	@ 0x184
4000a4a8:	e51b2008 	ldr	r2, [fp, #-8]
4000a4ac:	e1520003 	cmp	r2, r3
4000a4b0:	3affffc9 	bcc	4000a3dc <dtb_get_reg_size+0x48>
4000a4b4:	e3a03000 	mov	r3, #0
4000a4b8:	e1a00003 	mov	r0, r3
4000a4bc:	e24bd004 	sub	sp, fp, #4
4000a4c0:	e59db000 	ldr	fp, [sp]
4000a4c4:	e28dd004 	add	sp, sp, #4
4000a4c8:	e49df004 	pop	{pc}		@ (ldr pc, [sp], #4)

4000a4cc <align_down>:
4000a4cc:	e52db004 	push	{fp}		@ (str fp, [sp, #-4]!)
4000a4d0:	e28db000 	add	fp, sp, #0
4000a4d4:	e24dd00c 	sub	sp, sp, #12
4000a4d8:	e50b0008 	str	r0, [fp, #-8]
4000a4dc:	e50b100c 	str	r1, [fp, #-12]
4000a4e0:	e51b300c 	ldr	r3, [fp, #-12]
4000a4e4:	e2632000 	rsb	r2, r3, #0
4000a4e8:	e51b3008 	ldr	r3, [fp, #-8]
4000a4ec:	e0033002 	and	r3, r3, r2
4000a4f0:	e1a00003 	mov	r0, r3
4000a4f4:	e28bd000 	add	sp, fp, #0
4000a4f8:	e49db004 	pop	{fp}		@ (ldr fp, [sp], #4)
4000a4fc:	e12fff1e 	bx	lr

4000a500 <align_up>:
4000a500:	e52db004 	push	{fp}		@ (str fp, [sp, #-4]!)
4000a504:	e28db000 	add	fp, sp, #0
4000a508:	e24dd00c 	sub	sp, sp, #12
4000a50c:	e50b0008 	str	r0, [fp, #-8]
4000a510:	e50b100c 	str	r1, [fp, #-12]
4000a514:	e51b2008 	ldr	r2, [fp, #-8]
4000a518:	e51b300c 	ldr	r3, [fp, #-12]
4000a51c:	e0823003 	add	r3, r2, r3
4000a520:	e2432001 	sub	r2, r3, #1
4000a524:	e51b300c 	ldr	r3, [fp, #-12]
4000a528:	e2633000 	rsb	r3, r3, #0
4000a52c:	e0033002 	and	r3, r3, r2
4000a530:	e1a00003 	mov	r0, r3
4000a534:	e28bd000 	add	sp, fp, #0
4000a538:	e49db004 	pop	{fp}		@ (ldr fp, [sp], #4)
4000a53c:	e12fff1e 	bx	lr

4000a540 <mark>:
4000a540:	e52db008 	str	fp, [sp, #-8]!
4000a544:	e58de004 	str	lr, [sp, #4]
4000a548:	e28db004 	add	fp, sp, #4
4000a54c:	e24dd030 	sub	sp, sp, #48	@ 0x30
4000a550:	e50b0030 	str	r0, [fp, #-48]	@ 0xffffffd0
4000a554:	e50b1034 	str	r1, [fp, #-52]	@ 0xffffffcc
4000a558:	e51b2030 	ldr	r2, [fp, #-48]	@ 0xffffffd0
4000a55c:	e51b3034 	ldr	r3, [fp, #-52]	@ 0xffffffcc
4000a560:	e1520003 	cmp	r2, r3
4000a564:	3a000001 	bcc	4000a570 <mark+0x30>
4000a568:	e3e03000 	mvn	r3, #0
4000a56c:	ea000064 	b	4000a704 <mark+0x1c4>
4000a570:	e3a01a01 	mov	r1, #4096	@ 0x1000
4000a574:	e51b0030 	ldr	r0, [fp, #-48]	@ 0xffffffd0
4000a578:	ebffffd3 	bl	4000a4cc <align_down>
4000a57c:	e50b000c 	str	r0, [fp, #-12]
4000a580:	e3a01a01 	mov	r1, #4096	@ 0x1000
4000a584:	e51b0034 	ldr	r0, [fp, #-52]	@ 0xffffffcc
4000a588:	ebffffdc 	bl	4000a500 <align_up>
4000a58c:	e50b0010 	str	r0, [fp, #-16]
4000a590:	e51b300c 	ldr	r3, [fp, #-12]
4000a594:	e1a03623 	lsr	r3, r3, #12
4000a598:	e50b3014 	str	r3, [fp, #-20]	@ 0xffffffec
4000a59c:	e51b3010 	ldr	r3, [fp, #-16]
4000a5a0:	e1a03623 	lsr	r3, r3, #12
4000a5a4:	e50b3018 	str	r3, [fp, #-24]	@ 0xffffffe8
4000a5a8:	e30c31f0 	movw	r3, #49648	@ 0xc1f0
4000a5ac:	e3443001 	movt	r3, #16385	@ 0x4001
4000a5b0:	e5933000 	ldr	r3, [r3]
4000a5b4:	e51b2014 	ldr	r2, [fp, #-20]	@ 0xffffffec
4000a5b8:	e1520003 	cmp	r2, r3
4000a5bc:	3a000005 	bcc	4000a5d8 <mark+0x98>
4000a5c0:	e30c31f0 	movw	r3, #49648	@ 0xc1f0
4000a5c4:	e3443001 	movt	r3, #16385	@ 0x4001
4000a5c8:	e5933004 	ldr	r3, [r3, #4]
4000a5cc:	e51b2018 	ldr	r2, [fp, #-24]	@ 0xffffffe8
4000a5d0:	e1520003 	cmp	r2, r3
4000a5d4:	9a000001 	bls	4000a5e0 <mark+0xa0>
4000a5d8:	e3e03000 	mvn	r3, #0
4000a5dc:	ea000048 	b	4000a704 <mark+0x1c4>
4000a5e0:	e51b3014 	ldr	r3, [fp, #-20]	@ 0xffffffec
4000a5e4:	e50b3008 	str	r3, [fp, #-8]
4000a5e8:	ea00003e 	b	4000a6e8 <mark+0x1a8>
4000a5ec:	e30c31f0 	movw	r3, #49648	@ 0xc1f0
4000a5f0:	e3443001 	movt	r3, #16385	@ 0x4001
4000a5f4:	e5933000 	ldr	r3, [r3]
4000a5f8:	e51b2008 	ldr	r2, [fp, #-8]
4000a5fc:	e0423003 	sub	r3, r2, r3
4000a600:	e50b301c 	str	r3, [fp, #-28]	@ 0xffffffe4
4000a604:	e51b301c 	ldr	r3, [fp, #-28]	@ 0xffffffe4
4000a608:	e1a031a3 	lsr	r3, r3, #3
4000a60c:	e50b3020 	str	r3, [fp, #-32]	@ 0xffffffe0
4000a610:	e51b301c 	ldr	r3, [fp, #-28]	@ 0xffffffe4
4000a614:	e2033007 	and	r3, r3, #7
4000a618:	e50b3024 	str	r3, [fp, #-36]	@ 0xffffffdc
4000a61c:	e30c31f0 	movw	r3, #49648	@ 0xc1f0
4000a620:	e3443001 	movt	r3, #16385	@ 0x4001
4000a624:	e5933014 	ldr	r3, [r3, #20]
4000a628:	e51b2020 	ldr	r2, [fp, #-32]	@ 0xffffffe0
4000a62c:	e1520003 	cmp	r2, r3
4000a630:	2a000031 	bcs	4000a6fc <mark+0x1bc>
4000a634:	e3a02001 	mov	r2, #1
4000a638:	e51b3024 	ldr	r3, [fp, #-36]	@ 0xffffffdc
4000a63c:	e1a03312 	lsl	r3, r2, r3
4000a640:	e54b3025 	strb	r3, [fp, #-37]	@ 0xffffffdb
4000a644:	e30c31f0 	movw	r3, #49648	@ 0xc1f0
4000a648:	e3443001 	movt	r3, #16385	@ 0x4001
4000a64c:	e5932010 	ldr	r2, [r3, #16]
4000a650:	e51b3020 	ldr	r3, [fp, #-32]	@ 0xffffffe0
4000a654:	e0823003 	add	r3, r2, r3
4000a658:	e5d32000 	ldrb	r2, [r3]
4000a65c:	e55b3025 	ldrb	r3, [fp, #-37]	@ 0xffffffdb
4000a660:	e0033002 	and	r3, r3, r2
4000a664:	e6ef3073 	uxtb	r3, r3
4000a668:	e3530000 	cmp	r3, #0
4000a66c:	1a00001a 	bne	4000a6dc <mark+0x19c>
4000a670:	e30c31f0 	movw	r3, #49648	@ 0xc1f0
4000a674:	e3443001 	movt	r3, #16385	@ 0x4001
4000a678:	e5932010 	ldr	r2, [r3, #16]
4000a67c:	e51b3020 	ldr	r3, [fp, #-32]	@ 0xffffffe0
4000a680:	e0823003 	add	r3, r2, r3
4000a684:	e5d31000 	ldrb	r1, [r3]
4000a688:	e30c31f0 	movw	r3, #49648	@ 0xc1f0
4000a68c:	e3443001 	movt	r3, #16385	@ 0x4001
4000a690:	e5932010 	ldr	r2, [r3, #16]
4000a694:	e51b3020 	ldr	r3, [fp, #-32]	@ 0xffffffe0
4000a698:	e0823003 	add	r3, r2, r3
4000a69c:	e55b2025 	ldrb	r2, [fp, #-37]	@ 0xffffffdb
4000a6a0:	e1812002 	orr	r2, r1, r2
4000a6a4:	e6ef2072 	uxtb	r2, r2
4000a6a8:	e5c32000 	strb	r2, [r3]
4000a6ac:	e30c31f0 	movw	r3, #49648	@ 0xc1f0
4000a6b0:	e3443001 	movt	r3, #16385	@ 0x4001
4000a6b4:	e593300c 	ldr	r3, [r3, #12]
4000a6b8:	e3530000 	cmp	r3, #0
4000a6bc:	0a000006 	beq	4000a6dc <mark+0x19c>
4000a6c0:	e30c31f0 	movw	r3, #49648	@ 0xc1f0
4000a6c4:	e3443001 	movt	r3, #16385	@ 0x4001
4000a6c8:	e593300c 	ldr	r3, [r3, #12]
4000a6cc:	e2432001 	sub	r2, r3, #1
4000a6d0:	e30c31f0 	movw	r3, #49648	@ 0xc1f0
4000a6d4:	e3443001 	movt	r3, #16385	@ 0x4001
4000a6d8:	e583200c 	str	r2, [r3, #12]
4000a6dc:	e51b3008 	ldr	r3, [fp, #-8]
4000a6e0:	e2833001 	add	r3, r3, #1
4000a6e4:	e50b3008 	str	r3, [fp, #-8]
4000a6e8:	e51b2008 	ldr	r2, [fp, #-8]
4000a6ec:	e51b3018 	ldr	r3, [fp, #-24]	@ 0xffffffe8
4000a6f0:	e1520003 	cmp	r2, r3
4000a6f4:	3affffbc 	bcc	4000a5ec <mark+0xac>
4000a6f8:	ea000000 	b	4000a700 <mark+0x1c0>
4000a6fc:	e320f000 	nop	{0}
4000a700:	e3a03000 	mov	r3, #0
4000a704:	e1a00003 	mov	r0, r3
4000a708:	e24bd004 	sub	sp, fp, #4
4000a70c:	e59db000 	ldr	fp, [sp]
4000a710:	e28dd004 	add	sp, sp, #4
4000a714:	e49df004 	pop	{pc}		@ (ldr pc, [sp], #4)

4000a718 <unmark>:
4000a718:	e52db008 	str	fp, [sp, #-8]!
4000a71c:	e58de004 	str	lr, [sp, #4]
4000a720:	e28db004 	add	fp, sp, #4
4000a724:	e24dd030 	sub	sp, sp, #48	@ 0x30
4000a728:	e50b0030 	str	r0, [fp, #-48]	@ 0xffffffd0
4000a72c:	e50b1034 	str	r1, [fp, #-52]	@ 0xffffffcc
4000a730:	e51b2030 	ldr	r2, [fp, #-48]	@ 0xffffffd0
4000a734:	e51b3034 	ldr	r3, [fp, #-52]	@ 0xffffffcc
4000a738:	e1520003 	cmp	r2, r3
4000a73c:	3a000001 	bcc	4000a748 <unmark+0x30>
4000a740:	e3e03000 	mvn	r3, #0
4000a744:	ea00006b 	b	4000a8f8 <unmark+0x1e0>
4000a748:	e3a01a01 	mov	r1, #4096	@ 0x1000
4000a74c:	e51b0030 	ldr	r0, [fp, #-48]	@ 0xffffffd0
4000a750:	ebffff5d 	bl	4000a4cc <align_down>
4000a754:	e50b000c 	str	r0, [fp, #-12]
4000a758:	e3a01a01 	mov	r1, #4096	@ 0x1000
4000a75c:	e51b0034 	ldr	r0, [fp, #-52]	@ 0xffffffcc
4000a760:	ebffff66 	bl	4000a500 <align_up>
4000a764:	e50b0010 	str	r0, [fp, #-16]
4000a768:	e51b300c 	ldr	r3, [fp, #-12]
4000a76c:	e1a03623 	lsr	r3, r3, #12
4000a770:	e50b3014 	str	r3, [fp, #-20]	@ 0xffffffec
4000a774:	e51b3010 	ldr	r3, [fp, #-16]
4000a778:	e1a03623 	lsr	r3, r3, #12
4000a77c:	e50b3018 	str	r3, [fp, #-24]	@ 0xffffffe8
4000a780:	e30c31f0 	movw	r3, #49648	@ 0xc1f0
4000a784:	e3443001 	movt	r3, #16385	@ 0x4001
4000a788:	e5933000 	ldr	r3, [r3]
4000a78c:	e51b2014 	ldr	r2, [fp, #-20]	@ 0xffffffec
4000a790:	e1520003 	cmp	r2, r3
4000a794:	3a000005 	bcc	4000a7b0 <unmark+0x98>
4000a798:	e30c31f0 	movw	r3, #49648	@ 0xc1f0
4000a79c:	e3443001 	movt	r3, #16385	@ 0x4001
4000a7a0:	e5933004 	ldr	r3, [r3, #4]
4000a7a4:	e51b2018 	ldr	r2, [fp, #-24]	@ 0xffffffe8
4000a7a8:	e1520003 	cmp	r2, r3
4000a7ac:	9a000001 	bls	4000a7b8 <unmark+0xa0>
4000a7b0:	e3e03000 	mvn	r3, #0
4000a7b4:	ea00004f 	b	4000a8f8 <unmark+0x1e0>
4000a7b8:	e51b3014 	ldr	r3, [fp, #-20]	@ 0xffffffec
4000a7bc:	e50b3008 	str	r3, [fp, #-8]
4000a7c0:	ea000045 	b	4000a8dc <unmark+0x1c4>
4000a7c4:	e30c31f0 	movw	r3, #49648	@ 0xc1f0
4000a7c8:	e3443001 	movt	r3, #16385	@ 0x4001
4000a7cc:	e5933000 	ldr	r3, [r3]
4000a7d0:	e51b2008 	ldr	r2, [fp, #-8]
4000a7d4:	e0423003 	sub	r3, r2, r3
4000a7d8:	e50b301c 	str	r3, [fp, #-28]	@ 0xffffffe4
4000a7dc:	e51b301c 	ldr	r3, [fp, #-28]	@ 0xffffffe4
4000a7e0:	e1a031a3 	lsr	r3, r3, #3
4000a7e4:	e50b3020 	str	r3, [fp, #-32]	@ 0xffffffe0
4000a7e8:	e51b301c 	ldr	r3, [fp, #-28]	@ 0xffffffe4
4000a7ec:	e2033007 	and	r3, r3, #7
4000a7f0:	e50b3024 	str	r3, [fp, #-36]	@ 0xffffffdc
4000a7f4:	e30c31f0 	movw	r3, #49648	@ 0xc1f0
4000a7f8:	e3443001 	movt	r3, #16385	@ 0x4001
4000a7fc:	e5933014 	ldr	r3, [r3, #20]
4000a800:	e51b2020 	ldr	r2, [fp, #-32]	@ 0xffffffe0
4000a804:	e1520003 	cmp	r2, r3
4000a808:	2a000038 	bcs	4000a8f0 <unmark+0x1d8>
4000a80c:	e3a02001 	mov	r2, #1
4000a810:	e51b3024 	ldr	r3, [fp, #-36]	@ 0xffffffdc
4000a814:	e1a03312 	lsl	r3, r2, r3
4000a818:	e54b3025 	strb	r3, [fp, #-37]	@ 0xffffffdb
4000a81c:	e30c31f0 	movw	r3, #49648	@ 0xc1f0
4000a820:	e3443001 	movt	r3, #16385	@ 0x4001
4000a824:	e5932010 	ldr	r2, [r3, #16]
4000a828:	e51b3020 	ldr	r3, [fp, #-32]	@ 0xffffffe0
4000a82c:	e0823003 	add	r3, r2, r3
4000a830:	e5d32000 	ldrb	r2, [r3]
4000a834:	e55b3025 	ldrb	r3, [fp, #-37]	@ 0xffffffdb
4000a838:	e0033002 	and	r3, r3, r2
4000a83c:	e6ef3073 	uxtb	r3, r3
4000a840:	e3530000 	cmp	r3, #0
4000a844:	0a000021 	beq	4000a8d0 <unmark+0x1b8>
4000a848:	e30c31f0 	movw	r3, #49648	@ 0xc1f0
4000a84c:	e3443001 	movt	r3, #16385	@ 0x4001
4000a850:	e5932010 	ldr	r2, [r3, #16]
4000a854:	e51b3020 	ldr	r3, [fp, #-32]	@ 0xffffffe0
4000a858:	e0823003 	add	r3, r2, r3
4000a85c:	e5d33000 	ldrb	r3, [r3]
4000a860:	e6af2073 	sxtb	r2, r3
4000a864:	e15b32d5 	ldrsb	r3, [fp, #-37]	@ 0xffffffdb
4000a868:	e1e03003 	mvn	r3, r3
4000a86c:	e6af3073 	sxtb	r3, r3
4000a870:	e0033002 	and	r3, r3, r2
4000a874:	e6af1073 	sxtb	r1, r3
4000a878:	e30c31f0 	movw	r3, #49648	@ 0xc1f0
4000a87c:	e3443001 	movt	r3, #16385	@ 0x4001
4000a880:	e5932010 	ldr	r2, [r3, #16]
4000a884:	e51b3020 	ldr	r3, [fp, #-32]	@ 0xffffffe0
4000a888:	e0823003 	add	r3, r2, r3
4000a88c:	e6ef2071 	uxtb	r2, r1
4000a890:	e5c32000 	strb	r2, [r3]
4000a894:	e30c31f0 	movw	r3, #49648	@ 0xc1f0
4000a898:	e3443001 	movt	r3, #16385	@ 0x4001
4000a89c:	e593200c 	ldr	r2, [r3, #12]
4000a8a0:	e30c31f0 	movw	r3, #49648	@ 0xc1f0
4000a8a4:	e3443001 	movt	r3, #16385	@ 0x4001
4000a8a8:	e5933008 	ldr	r3, [r3, #8]
4000a8ac:	e1520003 	cmp	r2, r3
4000a8b0:	2a000006 	bcs	4000a8d0 <unmark+0x1b8>
4000a8b4:	e30c31f0 	movw	r3, #49648	@ 0xc1f0
4000a8b8:	e3443001 	movt	r3, #16385	@ 0x4001
4000a8bc:	e593300c 	ldr	r3, [r3, #12]
4000a8c0:	e2832001 	add	r2, r3, #1
4000a8c4:	e30c31f0 	movw	r3, #49648	@ 0xc1f0
4000a8c8:	e3443001 	movt	r3, #16385	@ 0x4001
4000a8cc:	e583200c 	str	r2, [r3, #12]
4000a8d0:	e51b3008 	ldr	r3, [fp, #-8]
4000a8d4:	e2833001 	add	r3, r3, #1
4000a8d8:	e50b3008 	str	r3, [fp, #-8]
4000a8dc:	e51b2008 	ldr	r2, [fp, #-8]
4000a8e0:	e51b3018 	ldr	r3, [fp, #-24]	@ 0xffffffe8
4000a8e4:	e1520003 	cmp	r2, r3
4000a8e8:	3affffb5 	bcc	4000a7c4 <unmark+0xac>
4000a8ec:	ea000000 	b	4000a8f4 <unmark+0x1dc>
4000a8f0:	e320f000 	nop	{0}
4000a8f4:	e3a03000 	mov	r3, #0
4000a8f8:	e1a00003 	mov	r0, r3
4000a8fc:	e24bd004 	sub	sp, fp, #4
4000a900:	e59db000 	ldr	fp, [sp]
4000a904:	e28dd004 	add	sp, sp, #4
4000a908:	e49df004 	pop	{pc}		@ (ldr pc, [sp], #4)

4000a90c <alloc_page>:
4000a90c:	e52db004 	push	{fp}		@ (str fp, [sp, #-4]!)
4000a910:	e28db000 	add	fp, sp, #0
4000a914:	e24dd024 	sub	sp, sp, #36	@ 0x24
4000a918:	e30c31f0 	movw	r3, #49648	@ 0xc1f0
4000a91c:	e3443001 	movt	r3, #16385	@ 0x4001
4000a920:	e593300c 	ldr	r3, [r3, #12]
4000a924:	e3530000 	cmp	r3, #0
4000a928:	1a000001 	bne	4000a934 <alloc_page+0x28>
4000a92c:	e3a03000 	mov	r3, #0
4000a930:	ea00005b 	b	4000aaa4 <alloc_page+0x198>
4000a934:	e30c31f0 	movw	r3, #49648	@ 0xc1f0
4000a938:	e3443001 	movt	r3, #16385	@ 0x4001
4000a93c:	e5933008 	ldr	r3, [r3, #8]
4000a940:	e50b3010 	str	r3, [fp, #-16]
4000a944:	e3a03000 	mov	r3, #0
4000a948:	e50b3008 	str	r3, [fp, #-8]
4000a94c:	ea00004d 	b	4000aa88 <alloc_page+0x17c>
4000a950:	e30c31f0 	movw	r3, #49648	@ 0xc1f0
4000a954:	e3443001 	movt	r3, #16385	@ 0x4001
4000a958:	e5932010 	ldr	r2, [r3, #16]
4000a95c:	e51b3008 	ldr	r3, [fp, #-8]
4000a960:	e0823003 	add	r3, r2, r3
4000a964:	e5d33000 	ldrb	r3, [r3]
4000a968:	e54b3011 	strb	r3, [fp, #-17]	@ 0xffffffef
4000a96c:	e55b3011 	ldrb	r3, [fp, #-17]	@ 0xffffffef
4000a970:	e35300ff 	cmp	r3, #255	@ 0xff
4000a974:	0a00003d 	beq	4000aa70 <alloc_page+0x164>
4000a978:	e3a03000 	mov	r3, #0
4000a97c:	e50b300c 	str	r3, [fp, #-12]
4000a980:	ea000036 	b	4000aa60 <alloc_page+0x154>
4000a984:	e51b3008 	ldr	r3, [fp, #-8]
4000a988:	e1a02183 	lsl	r2, r3, #3
4000a98c:	e51b300c 	ldr	r3, [fp, #-12]
4000a990:	e0823003 	add	r3, r2, r3
4000a994:	e50b3018 	str	r3, [fp, #-24]	@ 0xffffffe8
4000a998:	e51b2018 	ldr	r2, [fp, #-24]	@ 0xffffffe8
4000a99c:	e51b3010 	ldr	r3, [fp, #-16]
4000a9a0:	e1520003 	cmp	r2, r3
4000a9a4:	2a000033 	bcs	4000aa78 <alloc_page+0x16c>
4000a9a8:	e3a02001 	mov	r2, #1
4000a9ac:	e51b300c 	ldr	r3, [fp, #-12]
4000a9b0:	e1a03312 	lsl	r3, r2, r3
4000a9b4:	e54b3019 	strb	r3, [fp, #-25]	@ 0xffffffe7
4000a9b8:	e55b2011 	ldrb	r2, [fp, #-17]	@ 0xffffffef
4000a9bc:	e55b3019 	ldrb	r3, [fp, #-25]	@ 0xffffffe7
4000a9c0:	e0033002 	and	r3, r3, r2
4000a9c4:	e6ef3073 	uxtb	r3, r3
4000a9c8:	e3530000 	cmp	r3, #0
4000a9cc:	1a000020 	bne	4000aa54 <alloc_page+0x148>
4000a9d0:	e30c31f0 	movw	r3, #49648	@ 0xc1f0
4000a9d4:	e3443001 	movt	r3, #16385	@ 0x4001
4000a9d8:	e5932010 	ldr	r2, [r3, #16]
4000a9dc:	e51b3008 	ldr	r3, [fp, #-8]
4000a9e0:	e0823003 	add	r3, r2, r3
4000a9e4:	e5d31000 	ldrb	r1, [r3]
4000a9e8:	e30c31f0 	movw	r3, #49648	@ 0xc1f0
4000a9ec:	e3443001 	movt	r3, #16385	@ 0x4001
4000a9f0:	e5932010 	ldr	r2, [r3, #16]
4000a9f4:	e51b3008 	ldr	r3, [fp, #-8]
4000a9f8:	e0823003 	add	r3, r2, r3
4000a9fc:	e55b2019 	ldrb	r2, [fp, #-25]	@ 0xffffffe7
4000aa00:	e1812002 	orr	r2, r1, r2
4000aa04:	e6ef2072 	uxtb	r2, r2
4000aa08:	e5c32000 	strb	r2, [r3]
4000aa0c:	e30c31f0 	movw	r3, #49648	@ 0xc1f0
4000aa10:	e3443001 	movt	r3, #16385	@ 0x4001
4000aa14:	e593300c 	ldr	r3, [r3, #12]
4000aa18:	e2432001 	sub	r2, r3, #1
4000aa1c:	e30c31f0 	movw	r3, #49648	@ 0xc1f0
4000aa20:	e3443001 	movt	r3, #16385	@ 0x4001
4000aa24:	e583200c 	str	r2, [r3, #12]
4000aa28:	e30c31f0 	movw	r3, #49648	@ 0xc1f0
4000aa2c:	e3443001 	movt	r3, #16385	@ 0x4001
4000aa30:	e5933000 	ldr	r3, [r3]
4000aa34:	e51b2018 	ldr	r2, [fp, #-24]	@ 0xffffffe8
4000aa38:	e0823003 	add	r3, r2, r3
4000aa3c:	e50b3020 	str	r3, [fp, #-32]	@ 0xffffffe0
4000aa40:	e51b3020 	ldr	r3, [fp, #-32]	@ 0xffffffe0
4000aa44:	e1a03603 	lsl	r3, r3, #12
4000aa48:	e50b3024 	str	r3, [fp, #-36]	@ 0xffffffdc
4000aa4c:	e51b3024 	ldr	r3, [fp, #-36]	@ 0xffffffdc
4000aa50:	ea000013 	b	4000aaa4 <alloc_page+0x198>
4000aa54:	e51b300c 	ldr	r3, [fp, #-12]
4000aa58:	e2833001 	add	r3, r3, #1
4000aa5c:	e50b300c 	str	r3, [fp, #-12]
4000aa60:	e51b300c 	ldr	r3, [fp, #-12]
4000aa64:	e3530007 	cmp	r3, #7
4000aa68:	daffffc5 	ble	4000a984 <alloc_page+0x78>
4000aa6c:	ea000002 	b	4000aa7c <alloc_page+0x170>
4000aa70:	e320f000 	nop	{0}
4000aa74:	ea000000 	b	4000aa7c <alloc_page+0x170>
4000aa78:	e320f000 	nop	{0}
4000aa7c:	e51b3008 	ldr	r3, [fp, #-8]
4000aa80:	e2833001 	add	r3, r3, #1
4000aa84:	e50b3008 	str	r3, [fp, #-8]
4000aa88:	e30c31f0 	movw	r3, #49648	@ 0xc1f0
4000aa8c:	e3443001 	movt	r3, #16385	@ 0x4001
4000aa90:	e5933014 	ldr	r3, [r3, #20]
4000aa94:	e51b2008 	ldr	r2, [fp, #-8]
4000aa98:	e1520003 	cmp	r2, r3
4000aa9c:	3affffab 	bcc	4000a950 <alloc_page+0x44>
4000aaa0:	e3a03000 	mov	r3, #0
4000aaa4:	e1a00003 	mov	r0, r3
4000aaa8:	e28bd000 	add	sp, fp, #0
4000aaac:	e49db004 	pop	{fp}		@ (ldr fp, [sp], #4)
4000aab0:	e12fff1e 	bx	lr

4000aab4 <free_page>:
4000aab4:	e52db004 	push	{fp}		@ (str fp, [sp, #-4]!)
4000aab8:	e28db000 	add	fp, sp, #0
4000aabc:	e24dd024 	sub	sp, sp, #36	@ 0x24
4000aac0:	e50b0020 	str	r0, [fp, #-32]	@ 0xffffffe0
4000aac4:	e51b3020 	ldr	r3, [fp, #-32]	@ 0xffffffe0
4000aac8:	e7eb3053 	ubfx	r3, r3, #0, #12
4000aacc:	e3530000 	cmp	r3, #0
4000aad0:	0a000001 	beq	4000aadc <free_page+0x28>
4000aad4:	e3a03001 	mov	r3, #1
4000aad8:	ea000050 	b	4000ac20 <free_page+0x16c>
4000aadc:	e51b3020 	ldr	r3, [fp, #-32]	@ 0xffffffe0
4000aae0:	e1a03623 	lsr	r3, r3, #12
4000aae4:	e50b3008 	str	r3, [fp, #-8]
4000aae8:	e30c31f0 	movw	r3, #49648	@ 0xc1f0
4000aaec:	e3443001 	movt	r3, #16385	@ 0x4001
4000aaf0:	e5933000 	ldr	r3, [r3]
4000aaf4:	e51b2008 	ldr	r2, [fp, #-8]
4000aaf8:	e1520003 	cmp	r2, r3
4000aafc:	3a000005 	bcc	4000ab18 <free_page+0x64>
4000ab00:	e30c31f0 	movw	r3, #49648	@ 0xc1f0
4000ab04:	e3443001 	movt	r3, #16385	@ 0x4001
4000ab08:	e5933004 	ldr	r3, [r3, #4]
4000ab0c:	e51b2008 	ldr	r2, [fp, #-8]
4000ab10:	e1520003 	cmp	r2, r3
4000ab14:	3a000001 	bcc	4000ab20 <free_page+0x6c>
4000ab18:	e3a03001 	mov	r3, #1
4000ab1c:	ea00003f 	b	4000ac20 <free_page+0x16c>
4000ab20:	e30c31f0 	movw	r3, #49648	@ 0xc1f0
4000ab24:	e3443001 	movt	r3, #16385	@ 0x4001
4000ab28:	e5933000 	ldr	r3, [r3]
4000ab2c:	e51b2008 	ldr	r2, [fp, #-8]
4000ab30:	e0423003 	sub	r3, r2, r3
4000ab34:	e50b300c 	str	r3, [fp, #-12]
4000ab38:	e51b300c 	ldr	r3, [fp, #-12]
4000ab3c:	e1a031a3 	lsr	r3, r3, #3
4000ab40:	e50b3010 	str	r3, [fp, #-16]
4000ab44:	e51b300c 	ldr	r3, [fp, #-12]
4000ab48:	e2033007 	and	r3, r3, #7
4000ab4c:	e50b3014 	str	r3, [fp, #-20]	@ 0xffffffec
4000ab50:	e30c31f0 	movw	r3, #49648	@ 0xc1f0
4000ab54:	e3443001 	movt	r3, #16385	@ 0x4001
4000ab58:	e5933014 	ldr	r3, [r3, #20]
4000ab5c:	e51b2010 	ldr	r2, [fp, #-16]
4000ab60:	e1520003 	cmp	r2, r3
4000ab64:	3a000001 	bcc	4000ab70 <free_page+0xbc>
4000ab68:	e3a03001 	mov	r3, #1
4000ab6c:	ea00002b 	b	4000ac20 <free_page+0x16c>
4000ab70:	e3a02001 	mov	r2, #1
4000ab74:	e51b3014 	ldr	r3, [fp, #-20]	@ 0xffffffec
4000ab78:	e1a03312 	lsl	r3, r2, r3
4000ab7c:	e54b3015 	strb	r3, [fp, #-21]	@ 0xffffffeb
4000ab80:	e30c31f0 	movw	r3, #49648	@ 0xc1f0
4000ab84:	e3443001 	movt	r3, #16385	@ 0x4001
4000ab88:	e5932010 	ldr	r2, [r3, #16]
4000ab8c:	e51b3010 	ldr	r3, [fp, #-16]
4000ab90:	e0823003 	add	r3, r2, r3
4000ab94:	e5d32000 	ldrb	r2, [r3]
4000ab98:	e55b3015 	ldrb	r3, [fp, #-21]	@ 0xffffffeb
4000ab9c:	e0033002 	and	r3, r3, r2
4000aba0:	e6ef3073 	uxtb	r3, r3
4000aba4:	e3530000 	cmp	r3, #0
4000aba8:	0a00001b 	beq	4000ac1c <free_page+0x168>
4000abac:	e30c31f0 	movw	r3, #49648	@ 0xc1f0
4000abb0:	e3443001 	movt	r3, #16385	@ 0x4001
4000abb4:	e5932010 	ldr	r2, [r3, #16]
4000abb8:	e51b3010 	ldr	r3, [fp, #-16]
4000abbc:	e0823003 	add	r3, r2, r3
4000abc0:	e5d33000 	ldrb	r3, [r3]
4000abc4:	e6af2073 	sxtb	r2, r3
4000abc8:	e15b31d5 	ldrsb	r3, [fp, #-21]	@ 0xffffffeb
4000abcc:	e1e03003 	mvn	r3, r3
4000abd0:	e6af3073 	sxtb	r3, r3
4000abd4:	e0033002 	and	r3, r3, r2
4000abd8:	e6af1073 	sxtb	r1, r3
4000abdc:	e30c31f0 	movw	r3, #49648	@ 0xc1f0
4000abe0:	e3443001 	movt	r3, #16385	@ 0x4001
4000abe4:	e5932010 	ldr	r2, [r3, #16]
4000abe8:	e51b3010 	ldr	r3, [fp, #-16]
4000abec:	e0823003 	add	r3, r2, r3
4000abf0:	e6ef2071 	uxtb	r2, r1
4000abf4:	e5c32000 	strb	r2, [r3]
4000abf8:	e30c31f0 	movw	r3, #49648	@ 0xc1f0
4000abfc:	e3443001 	movt	r3, #16385	@ 0x4001
4000ac00:	e593300c 	ldr	r3, [r3, #12]
4000ac04:	e2832001 	add	r2, r3, #1
4000ac08:	e30c31f0 	movw	r3, #49648	@ 0xc1f0
4000ac0c:	e3443001 	movt	r3, #16385	@ 0x4001
4000ac10:	e583200c 	str	r2, [r3, #12]
4000ac14:	e3a03000 	mov	r3, #0
4000ac18:	ea000000 	b	4000ac20 <free_page+0x16c>
4000ac1c:	e3e03000 	mvn	r3, #0
4000ac20:	e1a00003 	mov	r0, r3
4000ac24:	e28bd000 	add	sp, fp, #0
4000ac28:	e49db004 	pop	{fp}		@ (ldr fp, [sp], #4)
4000ac2c:	e12fff1e 	bx	lr

4000ac30 <read_cpsr>:
4000ac30:	e52db004 	push	{fp}		@ (str fp, [sp, #-4]!)
4000ac34:	e28db000 	add	fp, sp, #0
4000ac38:	e24dd00c 	sub	sp, sp, #12
4000ac3c:	e10f3000 	mrs	r3, CPSR
4000ac40:	e50b3008 	str	r3, [fp, #-8]
4000ac44:	e51b3008 	ldr	r3, [fp, #-8]
4000ac48:	e1a00003 	mov	r0, r3
4000ac4c:	e28bd000 	add	sp, fp, #0
4000ac50:	e49db004 	pop	{fp}		@ (ldr fp, [sp], #4)
4000ac54:	e12fff1e 	bx	lr

4000ac58 <cpu_mode_from_cpsr>:
4000ac58:	e52db004 	push	{fp}		@ (str fp, [sp, #-4]!)
4000ac5c:	e28db000 	add	fp, sp, #0
4000ac60:	e24dd00c 	sub	sp, sp, #12
4000ac64:	e50b0008 	str	r0, [fp, #-8]
4000ac68:	e51b3008 	ldr	r3, [fp, #-8]
4000ac6c:	e203301f 	and	r3, r3, #31
4000ac70:	e1a00003 	mov	r0, r3
4000ac74:	e28bd000 	add	sp, fp, #0
4000ac78:	e49db004 	pop	{fp}		@ (ldr fp, [sp], #4)
4000ac7c:	e12fff1e 	bx	lr

4000ac80 <cpu_mode_str>:
4000ac80:	e52db004 	push	{fp}		@ (str fp, [sp, #-4]!)
4000ac84:	e28db000 	add	fp, sp, #0
4000ac88:	e24dd00c 	sub	sp, sp, #12
4000ac8c:	e50b0008 	str	r0, [fp, #-8]
4000ac90:	e51b3008 	ldr	r3, [fp, #-8]
4000ac94:	e353001f 	cmp	r3, #31
4000ac98:	0a000033 	beq	4000ad6c <cpu_mode_str+0xec>
4000ac9c:	e51b3008 	ldr	r3, [fp, #-8]
4000aca0:	e353001f 	cmp	r3, #31
4000aca4:	8a000033 	bhi	4000ad78 <cpu_mode_str+0xf8>
4000aca8:	e51b3008 	ldr	r3, [fp, #-8]
4000acac:	e353001b 	cmp	r3, #27
4000acb0:	0a00002a 	beq	4000ad60 <cpu_mode_str+0xe0>
4000acb4:	e51b3008 	ldr	r3, [fp, #-8]
4000acb8:	e353001b 	cmp	r3, #27
4000acbc:	8a00002d 	bhi	4000ad78 <cpu_mode_str+0xf8>
4000acc0:	e51b3008 	ldr	r3, [fp, #-8]
4000acc4:	e3530017 	cmp	r3, #23
4000acc8:	0a000021 	beq	4000ad54 <cpu_mode_str+0xd4>
4000accc:	e51b3008 	ldr	r3, [fp, #-8]
4000acd0:	e3530017 	cmp	r3, #23
4000acd4:	8a000027 	bhi	4000ad78 <cpu_mode_str+0xf8>
4000acd8:	e51b3008 	ldr	r3, [fp, #-8]
4000acdc:	e3530013 	cmp	r3, #19
4000ace0:	0a000018 	beq	4000ad48 <cpu_mode_str+0xc8>
4000ace4:	e51b3008 	ldr	r3, [fp, #-8]
4000ace8:	e3530013 	cmp	r3, #19
4000acec:	8a000021 	bhi	4000ad78 <cpu_mode_str+0xf8>
4000acf0:	e51b3008 	ldr	r3, [fp, #-8]
4000acf4:	e3530012 	cmp	r3, #18
4000acf8:	0a00000f 	beq	4000ad3c <cpu_mode_str+0xbc>
4000acfc:	e51b3008 	ldr	r3, [fp, #-8]
4000ad00:	e3530012 	cmp	r3, #18
4000ad04:	8a00001b 	bhi	4000ad78 <cpu_mode_str+0xf8>
4000ad08:	e51b3008 	ldr	r3, [fp, #-8]
4000ad0c:	e3530010 	cmp	r3, #16
4000ad10:	0a000003 	beq	4000ad24 <cpu_mode_str+0xa4>
4000ad14:	e51b3008 	ldr	r3, [fp, #-8]
4000ad18:	e3530011 	cmp	r3, #17
4000ad1c:	0a000003 	beq	4000ad30 <cpu_mode_str+0xb0>
4000ad20:	ea000014 	b	4000ad78 <cpu_mode_str+0xf8>
4000ad24:	e30b3148 	movw	r3, #45384	@ 0xb148
4000ad28:	e3443000 	movt	r3, #16384	@ 0x4000
4000ad2c:	ea000013 	b	4000ad80 <cpu_mode_str+0x100>
4000ad30:	e30b3150 	movw	r3, #45392	@ 0xb150
4000ad34:	e3443000 	movt	r3, #16384	@ 0x4000
4000ad38:	ea000010 	b	4000ad80 <cpu_mode_str+0x100>
4000ad3c:	e30b3154 	movw	r3, #45396	@ 0xb154
4000ad40:	e3443000 	movt	r3, #16384	@ 0x4000
4000ad44:	ea00000d 	b	4000ad80 <cpu_mode_str+0x100>
4000ad48:	e30b3158 	movw	r3, #45400	@ 0xb158
4000ad4c:	e3443000 	movt	r3, #16384	@ 0x4000
4000ad50:	ea00000a 	b	4000ad80 <cpu_mode_str+0x100>
4000ad54:	e30b3164 	movw	r3, #45412	@ 0xb164
4000ad58:	e3443000 	movt	r3, #16384	@ 0x4000
4000ad5c:	ea000007 	b	4000ad80 <cpu_mode_str+0x100>
4000ad60:	e30b316c 	movw	r3, #45420	@ 0xb16c
4000ad64:	e3443000 	movt	r3, #16384	@ 0x4000
4000ad68:	ea000004 	b	4000ad80 <cpu_mode_str+0x100>
4000ad6c:	e30b3178 	movw	r3, #45432	@ 0xb178
4000ad70:	e3443000 	movt	r3, #16384	@ 0x4000
4000ad74:	ea000001 	b	4000ad80 <cpu_mode_str+0x100>
4000ad78:	e30b3180 	movw	r3, #45440	@ 0xb180
4000ad7c:	e3443000 	movt	r3, #16384	@ 0x4000
4000ad80:	e1a00003 	mov	r0, r3
4000ad84:	e28bd000 	add	sp, fp, #0
4000ad88:	e49db004 	pop	{fp}		@ (ldr fp, [sp], #4)
4000ad8c:	e12fff1e 	bx	lr

4000ad90 <print_logo>:
4000ad90:	e52db008 	str	fp, [sp, #-8]!
4000ad94:	e58de004 	str	lr, [sp, #4]
4000ad98:	e28db004 	add	fp, sp, #4
4000ad9c:	e24dd088 	sub	sp, sp, #136	@ 0x88
4000ada0:	e30b3664 	movw	r3, #46692	@ 0xb664
4000ada4:	e3443000 	movt	r3, #16384	@ 0x4000
4000ada8:	e24b0070 	sub	r0, fp, #112	@ 0x70
4000adac:	e1a01003 	mov	r1, r3
4000adb0:	e3a03058 	mov	r3, #88	@ 0x58
4000adb4:	e1a02003 	mov	r2, r3
4000adb8:	ebfff56a 	bl	40008368 <memcpy>
4000adbc:	e3a03016 	mov	r3, #22
4000adc0:	e50b3010 	str	r3, [fp, #-16]
4000adc4:	e30b3188 	movw	r3, #45448	@ 0xb188
4000adc8:	e3443000 	movt	r3, #16384	@ 0x4000
4000adcc:	e50b3014 	str	r3, [fp, #-20]	@ 0xffffffec
4000add0:	e3a03000 	mov	r3, #0
4000add4:	e50b3008 	str	r3, [fp, #-8]
4000add8:	ea00000c 	b	4000ae10 <print_logo+0x80>
4000addc:	e51b3008 	ldr	r3, [fp, #-8]
4000ade0:	e1a03103 	lsl	r3, r3, #2
4000ade4:	e2433004 	sub	r3, r3, #4
4000ade8:	e083300b 	add	r3, r3, fp
4000adec:	e513306c 	ldr	r3, [r3, #-108]	@ 0xffffff94
4000adf0:	e1a02003 	mov	r2, r3
4000adf4:	e51b1014 	ldr	r1, [fp, #-20]	@ 0xffffffec
4000adf8:	e30b0190 	movw	r0, #45456	@ 0xb190
4000adfc:	e3440000 	movt	r0, #16384	@ 0x4000
4000ae00:	ebfff535 	bl	400082dc <kprintf>
4000ae04:	e51b3008 	ldr	r3, [fp, #-8]
4000ae08:	e2833001 	add	r3, r3, #1
4000ae0c:	e50b3008 	str	r3, [fp, #-8]
4000ae10:	e51b2008 	ldr	r2, [fp, #-8]
4000ae14:	e51b3010 	ldr	r3, [fp, #-16]
4000ae18:	e1520003 	cmp	r2, r3
4000ae1c:	baffffee 	blt	4000addc <print_logo+0x4c>
4000ae20:	e30b280c 	movw	r2, #47116	@ 0xb80c
4000ae24:	e3442000 	movt	r2, #16384	@ 0x4000
4000ae28:	e24b308c 	sub	r3, fp, #140	@ 0x8c
4000ae2c:	e1c200d0 	ldrd	r0, [r2]
4000ae30:	e1c300f0 	strd	r0, [r3]
4000ae34:	e1c200d8 	ldrd	r0, [r2, #8]
4000ae38:	e1c300f8 	strd	r0, [r3, #8]
4000ae3c:	e1c201d0 	ldrd	r0, [r2, #16]
4000ae40:	e1c301f0 	strd	r0, [r3, #16]
4000ae44:	e5922018 	ldr	r2, [r2, #24]
4000ae48:	e5832018 	str	r2, [r3, #24]
4000ae4c:	e3a03007 	mov	r3, #7
4000ae50:	e50b3018 	str	r3, [fp, #-24]	@ 0xffffffe8
4000ae54:	e30b019c 	movw	r0, #45468	@ 0xb19c
4000ae58:	e3440000 	movt	r0, #16384	@ 0x4000
4000ae5c:	ebfff51e 	bl	400082dc <kprintf>
4000ae60:	e3a03000 	mov	r3, #0
4000ae64:	e50b300c 	str	r3, [fp, #-12]
4000ae68:	ea00000b 	b	4000ae9c <print_logo+0x10c>
4000ae6c:	e51b300c 	ldr	r3, [fp, #-12]
4000ae70:	e1a03103 	lsl	r3, r3, #2
4000ae74:	e2433004 	sub	r3, r3, #4
4000ae78:	e083300b 	add	r3, r3, fp
4000ae7c:	e5133088 	ldr	r3, [r3, #-136]	@ 0xffffff78
4000ae80:	e1a01003 	mov	r1, r3
4000ae84:	e30b01a4 	movw	r0, #45476	@ 0xb1a4
4000ae88:	e3440000 	movt	r0, #16384	@ 0x4000
4000ae8c:	ebfff512 	bl	400082dc <kprintf>
4000ae90:	e51b300c 	ldr	r3, [fp, #-12]
4000ae94:	e2833001 	add	r3, r3, #1
4000ae98:	e50b300c 	str	r3, [fp, #-12]
4000ae9c:	e51b200c 	ldr	r2, [fp, #-12]
4000aea0:	e51b3018 	ldr	r3, [fp, #-24]	@ 0xffffffe8
4000aea4:	e1520003 	cmp	r2, r3
4000aea8:	3affffef 	bcc	4000ae6c <print_logo+0xdc>
4000aeac:	e30b01a8 	movw	r0, #45480	@ 0xb1a8
4000aeb0:	e3440000 	movt	r0, #16384	@ 0x4000
4000aeb4:	ebfff508 	bl	400082dc <kprintf>
4000aeb8:	e320f000 	nop	{0}
4000aebc:	e24bd004 	sub	sp, fp, #4
4000aec0:	e59db000 	ldr	fp, [sp]
4000aec4:	e28dd004 	add	sp, sp, #4
4000aec8:	e49df004 	pop	{pc}		@ (ldr pc, [sp], #4)

4000aecc <kmain>:
4000aecc:	e52db008 	str	fp, [sp, #-8]!
4000aed0:	e58de004 	str	lr, [sp, #4]
4000aed4:	e28db004 	add	fp, sp, #4
4000aed8:	e24dd010 	sub	sp, sp, #16
4000aedc:	e3080074 	movw	r0, #32884	@ 0x8074
4000aee0:	e3440000 	movt	r0, #16384	@ 0x4000
4000aee4:	ebfff4f0 	bl	400082ac <kprintf_init>
4000aee8:	e30b0828 	movw	r0, #47144	@ 0xb828
4000aeec:	e3440000 	movt	r0, #16384	@ 0x4000
4000aef0:	ebfff4f9 	bl	400082dc <kprintf>
4000aef4:	e30b084c 	movw	r0, #47180	@ 0xb84c
4000aef8:	e3440000 	movt	r0, #16384	@ 0x4000
4000aefc:	ebfff4f6 	bl	400082dc <kprintf>
4000af00:	e30b0878 	movw	r0, #47224	@ 0xb878
4000af04:	e3440000 	movt	r0, #16384	@ 0x4000
4000af08:	ebfff4f3 	bl	400082dc <kprintf>
4000af0c:	ebffff9f 	bl	4000ad90 <print_logo>
4000af10:	e30b1894 	movw	r1, #47252	@ 0xb894
4000af14:	e3441000 	movt	r1, #16384	@ 0x4000
4000af18:	e30b089c 	movw	r0, #47260	@ 0xb89c
4000af1c:	e3440000 	movt	r0, #16384	@ 0x4000
4000af20:	ebfff4ed 	bl	400082dc <kprintf>
4000af24:	e30b18bc 	movw	r1, #47292	@ 0xb8bc
4000af28:	e3441000 	movt	r1, #16384	@ 0x4000
4000af2c:	e30b08d0 	movw	r0, #47312	@ 0xb8d0
4000af30:	e3440000 	movt	r0, #16384	@ 0x4000
4000af34:	ebfff4e8 	bl	400082dc <kprintf>
4000af38:	e3a01080 	mov	r1, #128	@ 0x80
4000af3c:	e30b08f0 	movw	r0, #47344	@ 0xb8f0
4000af40:	e3440000 	movt	r0, #16384	@ 0x4000
4000af44:	ebfff4e4 	bl	400082dc <kprintf>
4000af48:	ebffff38 	bl	4000ac30 <read_cpsr>
4000af4c:	e50b0008 	str	r0, [fp, #-8]
4000af50:	e51b0008 	ldr	r0, [fp, #-8]
4000af54:	ebffff3f 	bl	4000ac58 <cpu_mode_from_cpsr>
4000af58:	e50b000c 	str	r0, [fp, #-12]
4000af5c:	e51b000c 	ldr	r0, [fp, #-12]
4000af60:	ebffff46 	bl	4000ac80 <cpu_mode_str>
4000af64:	e1a03000 	mov	r3, r0
4000af68:	e51b2008 	ldr	r2, [fp, #-8]
4000af6c:	e1a01003 	mov	r1, r3
4000af70:	e30b090c 	movw	r0, #47372	@ 0xb90c
4000af74:	e3440000 	movt	r0, #16384	@ 0x4000
4000af78:	ebfff4d7 	bl	400082dc <kprintf>
4000af7c:	e1a0300d 	mov	r3, sp
4000af80:	e50b3010 	str	r3, [fp, #-16]
4000af84:	e51b3010 	ldr	r3, [fp, #-16]
4000af88:	e1a01003 	mov	r1, r3
4000af8c:	e30b0938 	movw	r0, #47416	@ 0xb938
4000af90:	e3440000 	movt	r0, #16384	@ 0x4000
4000af94:	ebfff4d0 	bl	400082dc <kprintf>
4000af98:	e3081000 	movw	r1, #32768	@ 0x8000
4000af9c:	e3441000 	movt	r1, #16384	@ 0x4000
4000afa0:	e30b0950 	movw	r0, #47440	@ 0xb950
4000afa4:	e3440000 	movt	r0, #16384	@ 0x4000
4000afa8:	ebfff4cb 	bl	400082dc <kprintf>
4000afac:	e30c1208 	movw	r1, #49672	@ 0xc208
4000afb0:	e3441001 	movt	r1, #16385	@ 0x4001
4000afb4:	e30b0974 	movw	r0, #47476	@ 0xb974
4000afb8:	e3440000 	movt	r0, #16384	@ 0x4000
4000afbc:	ebfff4c6 	bl	400082dc <kprintf>
4000afc0:	e30c31f0 	movw	r3, #49648	@ 0xc1f0
4000afc4:	e3443001 	movt	r3, #16385	@ 0x4001
4000afc8:	e5933010 	ldr	r3, [r3, #16]
4000afcc:	e1a01003 	mov	r1, r3
4000afd0:	e30b0998 	movw	r0, #47512	@ 0xb998
4000afd4:	e3440000 	movt	r0, #16384	@ 0x4000
4000afd8:	ebfff4bf 	bl	400082dc <kprintf>
4000afdc:	e30c31f0 	movw	r3, #49648	@ 0xc1f0
4000afe0:	e3443001 	movt	r3, #16385	@ 0x4001
4000afe4:	e5932010 	ldr	r2, [r3, #16]
4000afe8:	e30c31f0 	movw	r3, #49648	@ 0xc1f0
4000afec:	e3443001 	movt	r3, #16385	@ 0x4001
4000aff0:	e5933014 	ldr	r3, [r3, #20]
4000aff4:	e0823003 	add	r3, r2, r3
4000aff8:	e1a01003 	mov	r1, r3
4000affc:	e30b09bc 	movw	r0, #47548	@ 0xb9bc
4000b000:	e3440000 	movt	r0, #16384	@ 0x4000
4000b004:	ebfff4b4 	bl	400082dc <kprintf>
4000b008:	e30c31f0 	movw	r3, #49648	@ 0xc1f0
4000b00c:	e3443001 	movt	r3, #16385	@ 0x4001
4000b010:	e593100c 	ldr	r1, [r3, #12]
4000b014:	e30c31f0 	movw	r3, #49648	@ 0xc1f0
4000b018:	e3443001 	movt	r3, #16385	@ 0x4001
4000b01c:	e5933008 	ldr	r3, [r3, #8]
4000b020:	e1a02003 	mov	r2, r3
4000b024:	e30b09e0 	movw	r0, #47584	@ 0xb9e0
4000b028:	e3440000 	movt	r0, #16384	@ 0x4000
4000b02c:	ebfff4aa 	bl	400082dc <kprintf>
4000b030:	e30b3bb0 	movw	r3, #48048	@ 0xbbb0
4000b034:	e3443000 	movt	r3, #16384	@ 0x4000
4000b038:	e5933008 	ldr	r3, [r3, #8]
4000b03c:	e1a01003 	mov	r1, r3
4000b040:	e30b0a14 	movw	r0, #47636	@ 0xba14
4000b044:	e3440000 	movt	r0, #16384	@ 0x4000
4000b048:	ebfff4a3 	bl	400082dc <kprintf>
4000b04c:	e30b3bb0 	movw	r3, #48048	@ 0xbbb0
4000b050:	e3443000 	movt	r3, #16384	@ 0x4000
4000b054:	e593300c 	ldr	r3, [r3, #12]
4000b058:	e1a01003 	mov	r1, r3
4000b05c:	e30b0a38 	movw	r0, #47672	@ 0xba38
4000b060:	e3440000 	movt	r0, #16384	@ 0x4000
4000b064:	ebfff49c 	bl	400082dc <kprintf>
4000b068:	e30b0a5c 	movw	r0, #47708	@ 0xba5c
4000b06c:	e3440000 	movt	r0, #16384	@ 0x4000
4000b070:	ebfff499 	bl	400082dc <kprintf>
4000b074:	e320f003 	wfi
4000b078:	eafffffd 	b	4000b074 <kmain+0x1a8>

4000b07c <_start>:
4000b07c:	e59fd034 	ldr	sp, [pc, #52]	@ 4000b0b8 <clear_bss+0x20>
4000b080:	f10c0080 	cpsid	i
4000b084:	f10c0040 	cpsid	f
4000b088:	eb000002 	bl	4000b098 <clear_bss>
4000b08c:	e3a00101 	mov	r0, #1073741824	@ 0x40000000
4000b090:	ebfff894 	bl	400092e8 <early>
4000b094:	eafffffe 	b	4000b094 <_start+0x18>

4000b098 <clear_bss>:
4000b098:	e59f001c 	ldr	r0, [pc, #28]	@ 4000b0bc <clear_bss+0x24>
4000b09c:	e59f101c 	ldr	r1, [pc, #28]	@ 4000b0c0 <clear_bss+0x28>
4000b0a0:	e3a03000 	mov	r3, #0
4000b0a4:	e1500001 	cmp	r0, r1
4000b0a8:	aa000001 	bge	4000b0b4 <clear_bss+0x1c>
4000b0ac:	e4803004 	str	r3, [r0], #4
4000b0b0:	eafffffb 	b	4000b0a4 <clear_bss+0xc>
4000b0b4:	e12fff1e 	bx	lr
4000b0b8:	40ffffff 	.word	0x40ffffff
4000b0bc:	4000baa0 	.word	0x4000baa0
4000b0c0:	4001c208 	.word	0x4001c208

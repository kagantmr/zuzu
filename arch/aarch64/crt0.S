/**
 * @file
 * @brief User program entry point.
 */

.text

/**
 * _start
 *
 * Entered from the kernel with x0 = argc and x1 = argv. Runs the constructors, calls
 * main(argc, argv), runs the destructors in reverse, and quits with main's return value
 * (svc #0 is SVC_QUIT). With newlib, __libc_init_array and exit() do the constructor and
 * destructor work instead. Never returns.
 */
.global _start
.type _start, %function
_start:
    mov x29, xzr            // end of the frame-pointer chain for backtraces
    mov x30, xzr
    mov x19, x0
    mov x20, x1

#ifdef ZUZU_NEWLIB
    bl __libc_init_array
    mov x0, x19
    mov x1, x20
    bl main
    bl exit
3:  b 3b

/** Empty _init and _fini, which newlib's __libc_init_array expects to exist. */
.global _init
.type _init, %function
_init:
    ret
.size _init, . - _init

.global _fini
.type _fini, %function
_fini:
    ret
.size _fini, . - _fini

#else
    adrp x21, __init_array_start
    add  x21, x21, :lo12:__init_array_start
    adrp x22, __init_array_end
    add  x22, x22, :lo12:__init_array_end
1:
    cmp  x21, x22
    b.hs 2f
    ldr  x3, [x21], #8      // load the next constructor, step 8 bytes
    blr  x3
    b    1b
2:
    mov x0, x19
    mov x1, x20
    bl  main
    mov x19, x0             // keep main's return value across the destructors

    adrp x21, __fini_array_start
    add  x21, x21, :lo12:__fini_array_start
    adrp x22, __fini_array_end
    add  x22, x22, :lo12:__fini_array_end
4:
    cmp  x22, x21
    b.ls 5f
    ldr  x3, [x22, #-8]!    // step back 8 bytes first, then load
    blr  x3
    b    4b
5:
    mov x0, x19
    svc #0                  // SVC_QUIT
3:  b 3b
#endif

.size _start, . - _start
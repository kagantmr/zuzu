/**
* @file aeabi.S
* @brief ARM function bindings for Clang
*/

.arch armv7-a

.arm
.type __aeabi_memcpy,%function
.type __aeabi_memcpy4,%function
.type __aeabi_memcpy8,%function

.type __aeabi_memmove,%function
.type __aeabi_memmove4,%function
.type __aeabi_memmove8,%function

.type __aeabi_memset,%function
.type __aeabi_memset4,%function
.type __aeabi_memset8,%function

.type __aeabi_memclr,%function
.type __aeabi_memclr4,%function
.type __aeabi_memclr8,%function

.global __aeabi_memcpy
.global __aeabi_memcpy4
.global __aeabi_memcpy8

.global __aeabi_memmove
.global __aeabi_memmove4
.global __aeabi_memmove8

.global __aeabi_memset
.global __aeabi_memset4
.global __aeabi_memset8

.global __aeabi_memclr
.global __aeabi_memclr4
.global __aeabi_memclr8

__aeabi_memcpy:
__aeabi_memcpy4:
__aeabi_memcpy8:
    b memcpy

__aeabi_memmove:
__aeabi_memmove4:
__aeabi_memmove8:
    b memmove

__aeabi_memset:
__aeabi_memset4:
__aeabi_memset8:
    mov r3, r2
    mov r2, r1
    mov r1, r3
    b memset

__aeabi_memclr:
__aeabi_memclr4:
__aeabi_memclr8:
    mov r2, r1
    mov r1, #0
    b memset
    
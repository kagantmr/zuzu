// context.h - ARM exception frame definition
// This file defines the structure of the exception frame that is saved on the stack when an exception
// occurs. The layout of this structure must match the assembly code in entry.S that saves the registers
// to the stack. The exception frame includes the general-purpose registers, user SP and LR saved via SRS, and the return PC and CPSR that the exception handler will use to return to user mode.

#ifndef CONTEXT_H
#define CONTEXT_H

#include <stdint.h>

/**
 * Represents a process's saved CPU state at the time of an exception.
 * Layout must match the stmfd sequence in entry.S exactly,
 * the assembly writes directly into this struct by offset.
 */
typedef struct exception_frame {
    uint32_t r[13];        /* r0-r12 */
    uint32_t sp_usr;       /* user SP saved via SRS */
    uint32_t lr_usr;       /* user LR saved via SRS */
    uint32_t return_pc;    /* adjusted return address (LR - offset) */
    uint32_t return_cpsr;  /* saved CPSR/SPSR value you return with */
} exception_frame_t;

#endif // CONTEXT_H

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

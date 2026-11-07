/**
 * @file
 * @brief Structure offsets for assembly, turned into asm-offsets.h by the build.
 */

#include "kernel/task/task.h"
#include <stddef.h>

/**
 * @brief Emits a "->sym value" marker into the assembly output, which the build turns into a
 * \#define.
 */
#define DEFINE(sym, val) __asm__ volatile("\n.ascii \"->" #sym " %c0\"" ::"i"(val))

/** @brief Holds the DEFINE() list; compiled to assembly only, never called. */
void AsmOffsets(void);
void AsmOffsets(void)
{
    DEFINE(TASK_KERNEL_SP, offsetof(TaskObject, kernel_sp));

    DEFINE(CPU_STATE_SIZE, sizeof(CpuState));
    DEFINE(CPU_STATE_X30, offsetof(CpuState, x[30]));
    DEFINE(CPU_STATE_SP_EL0, offsetof(CpuState, sp_el0));
    DEFINE(CPU_STATE_PC, offsetof(CpuState, return_pc));
    DEFINE(CPU_STATE_PSTATE, offsetof(CpuState, return_pstate));
}

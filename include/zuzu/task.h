#ifndef ZUZU_TASK_H
#define ZUZU_TASK_H

#include "zuzu/syscall_nums.h"
#include <zuzu/types.h>
#include <zuzu/err.h>
#include <arch/syscall.h>
#include <zuzu/spawn_args.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif



/* ---- Task lifecycle syscalls ---- */

/** 
 * @brief Terminates the current process with the specified exit status.
 * 
 * @param status The exit status of the process, visible by the parent.
 */
static inline void __attribute__((noreturn)) ZuzuPQuit(Err status) {
    Syscall(SYS_PQUIT, (uint32_t)status, 0, 0, 0);
    __builtin_unreachable();
}

/**
 * @brief Yields the CPU to allow other tasks to run.
 * 
 * @return int32_t Returns 0 on success. Cannot fail
 */
static inline Err ZuzuYield(void) {
    return Syscall(SYS_YIELD, 0, 0, 0, 0);
}

/**
 * @brief Waits for a child process to change state, with optional flags.
 * 
 * @param pid The process ID of the child to wait for, or -1 to wait for any child.
 * @param statusOut Pointer to an integer where the exit status will be stored.
 * @param flags Flags to modify the behavior of the wait (e.g., WNOHANG).
 * 
 * @return Err  
 */
static inline Err ZuzuWait(Pid pid, Err *statusOut, uint32_t flags) {
    return Syscall(SYS_WAIT, (uint32_t)pid, (uint32_t)(VirtAddr)statusOut, flags, 0);
}

/**
 *  @brief Retrieves the process ID of the calling process.
 */
static inline Pid ZuzuGetPid(void) {
    return Syscall(SYS_GETPID, 0, 0, 0, 0);
}

/**
 * @brief Suspends the calling process for a specified number of milliseconds.
 */
static inline Err ZuzuSleep(Duration ms) {
    return Syscall(SYS_SLEEP, ms, 0, 0, 0);
}

/**
 * @brief Spawns a new process with the specified name.
 * 
 * @param name The name of the process to spawn.
 * 
 * @return `TSpawnResult` Returns a structure containing the task handle and process ID of the newly spawned process.
 */
static inline TSpawnResult ZuzuPSpawn(const char* name) {
    size_t name_len = 0;
    while (name && name[name_len])
        name_len++;
    SpawnArgs args = {
        .size     = sizeof(SpawnArgs),
        .name     = name,
        .name_len = name_len,
    };
    Message result = syscall_msg(SYS_PSPAWN, (uint32_t)(VirtAddr)&args, 0, 0, 0);
    return (TSpawnResult) {.taskHandle = (Handle) result.w0, .pid = (Pid) result.w1};
}

/**
 * @brief Starts the child process whose address space has been filled.
 * 
 * @param elf_data Pointer to the ELF file data in memory.
 * @param elf_size Size of the ELF file data in bytes.
 * @param name Name of the process (null-terminated string).
 * 
 * @return 0 on success, negative error code on faiure.
 */
static inline Err ZuzuKickstart(Handle taskHandle, VirtAddr entry,
                                  VirtAddr sp, uint32_t r0_val, uint32_t r1_val) {
    KickstartArgs args = {
        .size        = sizeof(KickstartArgs),
        .taskHandle  = taskHandle,
        .entry       = entry,
        .sp          = sp,
        .r0_val      = r0_val,
        .r1_val      = r1_val,
    };
    return (Err) Syscall(SYS_KICKSTART, (uint32_t)(VirtAddr)&args, 0, 0, 0);
}

/**
 * @brief Kills the process associated with the specified task handle.
 */
static inline Err ZuzuPKill(Handle taskHandle) {
    return Syscall(SYS_PKILL, taskHandle, 0, 0, 0);
}

/**
 * @brief Creates a new thread in the current process with the specified entry point, stack pointer, and argument.
 */
static inline Tid ZuzuTMake(void (*entry)(void *), void *user_sp, void *arg) {
    return (Tid)Syscall(SYS_TMAKE, (uint32_t)(VirtAddr)entry, (uint32_t)(VirtAddr)user_sp,
                           (uint32_t)(VirtAddr)arg, 0);
}

/**
 * @brief Waits for the specified thread to terminate and retrieves its exit status.
 */
static inline Err ZuzuTJoin(Tid tid) {
    return Syscall(SYS_TJOIN, tid, 0, 0, 0);
}

/**
 * @brief Terminates the calling thread with the specified exit status.
 */
static inline __attribute__((noreturn)) void ZuzuTQuit(Err status) {
    Syscall(SYS_TQUIT, (uint32_t)status, 0, 0, 0);
    __builtin_unreachable();
}

#ifdef __cplusplus
}
#endif

#endif

#ifndef ZUZU_TASK_H
#define ZUZU_TASK_H

#include "zuzu/syscall_nums.h"
#include "zuzu/types.h"
#include <arch/syscall.h>
#include <zuzu/spawn_args.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Process constants ---- */

#define NAMETABLE_PID 1
#define WNOHANG (1 << 0)

/* ---- Task lifecycle syscalls ---- */

/** 
 * @brief Terminates the current process with the specified exit status.
 * 
 * @param status The exit status of the process.
 */
static inline void __attribute__((noreturn)) zuzu_pquit(int32_t status) {
    syscall(SYS_PQUIT, (uint32_t)status, 0, 0, 0);
    __builtin_unreachable();
}

/**
 * @brief Yields the CPU to allow other tasks to run.
 * 
 * @return int32_t Returns 0 on success, or a negative error code on failure.
 */
static inline int32_t zuzu_yield(void) {
    return syscall(SYS_YIELD, 0, 0, 0, 0);
}

/**
 * @brief Waits for a child process to change state, with optional flags.
 * 
 * @param pid The process ID of the child to wait for, or -1 to wait for any child.
 * @param status_out Pointer to an integer where the exit status will be stored.
 * @param flags Flags to modify the behavior of the wait (e.g., WNOHANG).
 * 
 * @return int32_t 
 */
static inline int32_t zuzu_wait(Pid pid, int32_t *status_out, uint32_t flags) {
    return syscall(SYS_WAIT, (uint32_t)pid, (uint32_t)(uintptr_t)status_out, flags, 0);
}

/**
 *  @brief Retrieves the process ID of the calling process.
 */
static inline int32_t zuzu_getpid(void) {
    return syscall(SYS_GETPID, 0, 0, 0, 0);
}

/**
 * @brief Suspends the calling process for a specified number of milliseconds.
 */
static inline int32_t zuzu_sleep(uint32_t ms) {
    return syscall(SYS_SLEEP, ms, 0, 0, 0);
}

/**
 * @brief Spawns a new process with the specified name.
 * 
 * @param name The name of the process to spawn.
 * @return TSpawnResult Returns a structure containing the task handle and process ID of the newly spawned process.
 */
static inline TSpawnResult zuzu_pspawn(const char* name) {
    size_t name_len = 0;
    while (name && name[name_len])
        name_len++;
    spawn_args_t args = {
        .size     = sizeof(spawn_args_t),
        .name     = name,
        .name_len = name_len,
    };
    Message result = syscall_msg(SYS_PSPAWN, (uint32_t)(uintptr_t)&args, 0, 0, 0);
    return (TSpawnResult) {.taskHandle = (Handle) result.w0, .pid = (Pid) result.w1};
}

/**
 * @brief Starts the child process whose address space has been filled.
 * 
 * @param elf_data Pointer to the ELF file data in memory.
 * @param elf_size Size of the ELF file data in bytes.
 * @param name Name of the process (null-terminated string).
 */
static inline Handle zuzu_kickstart(Handle taskHandle, uintptr_t entry,
                                  uintptr_t sp, uint32_t r0_val, uint32_t r1_val) {
    kickstart_args_t args = {
        .size        = sizeof(kickstart_args_t),
        .taskHandle = taskHandle,
        .entry       = entry,
        .sp          = sp,
        .r0_val      = r0_val,
        .r1_val      = r1_val,
    };
    return (Handle) syscall(SYS_KICKSTART, (uint32_t)(uintptr_t)&args, 0, 0, 0);
}

/**
 * @brief Kills the process associated with the specified task handle.
 */
static inline int32_t zuzu_pkill(Handle taskHandle) {
    return syscall(SYS_PKILL, taskHandle, 0, 0, 0);
}

/**
 * @brief Creates a new thread in the current process with the specified entry point, stack pointer, and argument.
 */
static inline Tid zuzu_tmake(void (*entry)(void *), void *user_sp, void *arg) {
    return (Tid)syscall(SYS_TMAKE, (uint32_t)(VirtAddr)entry, (uint32_t)(VirtAddr)user_sp,
                           (uint32_t)(VirtAddr)arg, 0);
}

/**
 * @brief Waits for the specified thread to terminate and retrieves its exit status.
 */
static inline int32_t zuzu_tjoin(Tid tid) {
    return syscall(SYS_TJOIN, tid, 0, 0, 0);
}

/**
 * @brief Terminates the calling thread with the specified exit status.
 */
static inline __attribute__((noreturn)) void zuzu_tquit(int32_t status) {
    syscall(SYS_TQUIT, (uint32_t)status, 0, 0, 0);
    __builtin_unreachable();
}

#ifdef __cplusplus
}
#endif

#endif

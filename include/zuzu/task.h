#ifndef ZUZU_TASK_H
#define ZUZU_TASK_H

#include "zuzu/syscall_nums.h"
#include "zuzu/types.h"
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
    register int32_t r0 __asm__("r0") = status;
    __asm__ volatile("svc %[num]"
        :
        : "r"(r0), [num] "i"(SYS_PQUIT)
        : "memory");
    __builtin_unreachable();
}

/**
 * @brief Yields the CPU to allow other tasks to run.
 * 
 * @return int32_t Returns 0 on success, or a negative error code on failure.
 */
static inline int32_t zuzu_yield(void) {
    register int32_t r0 __asm__("r0");
    __asm__ volatile("svc %[num]"
        : "=r"(r0)
        : [num] "i"(SYS_YIELD)
        : "memory");
    return r0;
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
static inline int32_t zuzu_wait(zpid_t pid, int32_t *status_out, uint32_t flags) {
    register zpid_t r0 __asm__("r0") = pid;
    register int32_t *r1 __asm__("r1") = status_out;
    register uint32_t r2 __asm__("r2") = flags;
    __asm__ volatile("svc %[num]"
        : "+r"(r0)
        : "r"(r1), "r"(r2), [num] "i"(SYS_WAIT)
        : "memory");
    return r0;
}

/**
 *  @brief Retrieves the process ID of the calling process.
 */
static inline int32_t zuzu_getpid(void) {
    register int32_t r0 __asm__("r0");
    __asm__ volatile("svc %[num]"
        : "=r"(r0)
        : [num] "i"(SYS_GETPID)
        : "memory");
    return r0;
}

/**
 * @brief Suspends the calling process for a specified number of milliseconds.
 */
static inline int32_t zuzu_sleep(uint32_t ms) {
    register uint32_t r0 __asm__("r0") = ms;
    __asm__ volatile("svc %[num]"
        : "+r"(r0)
        : [num] "i"(SYS_SLEEP)
        : "memory");
    return (int32_t) r0;
}

/**
 * @brief Spawns a new process with the specified name.
 * 
 * @param name The name of the process to spawn.
 * @return tspawn_result_t Returns a structure containing the task handle and process ID of the newly spawned process.
 */
static inline tspawn_result_t zuzu_pspawn(const char* name) {
    size_t name_len = 0;
    while (name && name[name_len])
        name_len++;
    spawn_args_t args = {
        .size     = sizeof(spawn_args_t),
        .name     = name,
        .name_len = name_len,
    };
    register uintptr_t r0 __asm__("r0") = (uintptr_t) &args;
    register zpid_t r1 __asm__("r1"); // pid
    __asm__ volatile("svc %[num]"
    : "+r"(r0), "=r"(r1)
    : [num] "i"(SYS_PSPAWN)
    : "memory");
    return (tspawn_result_t) {.task_handle = (handle_t) r0, .pid = r1};
}

/**
 * @brief Starts the child process whose address space has been filled.
 * 
 * @param elf_data Pointer to the ELF file data in memory.
 * @param elf_size Size of the ELF file data in bytes.
 * @param name Name of the process (null-terminated string).
 */
static inline handle_t zuzu_kickstart(handle_t task_handle, uintptr_t entry,
                                  uintptr_t sp, uint32_t r0_val, uint32_t r1_val) {
    kickstart_args_t args = {
        .size        = sizeof(kickstart_args_t),
        .task_handle = task_handle,
        .entry       = entry,
        .sp          = sp,
        .r0_val      = r0_val,
        .r1_val      = r1_val,
    };
    register uintptr_t r0 __asm__("r0") = (uintptr_t) &args;
    __asm__ volatile("svc %[num]"
        : "+r"(r0)
        : [num] "i"(SYS_KICKSTART)
        : "memory");
    return (handle_t) r0;
}

/**
 * @brief Kills the process associated with the specified task handle.
 */
static inline int32_t zuzu_pkill(handle_t task_handle) {
    register handle_t r0 __asm__("r0") = task_handle;
    __asm__ volatile("svc %[num]"
        : "+r"(r0)
        : [num] "i"(SYS_PKILL)
        : "memory");
    return r0;
}

/**
 * @brief Creates a new thread in the current process with the specified entry point, stack pointer, and argument.
 */
static inline tid_t zuzu_tmake(void (*entry)(void *), void *user_sp, void *arg) {
    register vaddr_t r0 __asm__("r0") = (vaddr_t)entry;
    register vaddr_t r1 __asm__("r1") = (vaddr_t)user_sp;
    register vaddr_t r2 __asm__("r2") = (vaddr_t)arg;
    __asm__ volatile("svc %[num]"
        : "+r"(r0)
        : "r"(r1), "r"(r2), [num] "i"(SYS_TMAKE)
        : "memory");
    return (tid_t)r0;
}

/**
 * @brief Waits for the specified thread to terminate and retrieves its exit status.
 */
static inline int32_t zuzu_tjoin(tid_t tid) {
    register tid_t r0 __asm__("r0") = tid;
    __asm__ volatile("svc %[num]"
        : "+r"(r0)
        : [num] "i"(SYS_TJOIN)
        : "memory");
    return r0;
}

/**
 * @brief Terminates the calling thread with the specified exit status.
 */
static inline __attribute__((noreturn)) void zuzu_tquit(int32_t status) {
    register int32_t r0 __asm__("r0") = status;
    __asm__ volatile("svc %[num]"
        :
        : "r"(r0), [num] "i"(SYS_TQUIT)
        : "memory");
    __builtin_unreachable();
}

#ifdef __cplusplus
}
#endif

#endif

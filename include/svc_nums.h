/**
 * @file svc_nums.h
 * @brief This file contains the system call/service ID numbers.
 */

#ifndef SYSCALL_NUMS_H
#define SYSCALL_NUMS_H

#include <stdint.h>

/** @brief System call numbers. */
typedef enum {
    SVC_QUIT = 0,     /**< Quit system call. */
    SVC_YIELD,        /**< Yield system call. */
    SVC_SLEEP,        /**< Sleep system call. */
    SVC_LOG,          /**< !!DEBUG-ONLY!! Log system call. */
    SVC_CREATE,       /**< Create system call. */
    SVC_CALL,         /**< Call system call. */
    SVC_REPLY,        /**< Reply system call. */
    SVC_WAITON,       /**< WaitOn system call. */
    SVC_MANAGEHANDLE, /**< ManageHandle system call. */
    SVC_SIGNAL,       /**< Signal system call. */
    SVC_BIND,         /**< Bind system call. */
    SVC_MANAGEMEMORY, /**< ManageMemory system call. */
    SVC_MANAGETASK,   /**< ManageTask system call. */
    SVC_HOLD,         /**< Hold system call. */
#ifdef CONFIG_ZUZU_BENCH
    SVC_BENCH, /**< !!DEBUG-ONLY!! Bench system call. */
#endif /* CONFIG_ZUZU_BENCH */
    SVC_TOTAL_COUNT /**< Number of system calls. */
} SvcNumber;

#ifdef CONFIG_ZUZU_BENCH
/** @brief DEBUG-ONLY!!! Bench operation code. */
typedef enum {
    BENCH_RESET, /**< Reset numbers. */
    BENCH_DUMP, /**< Dump measurements. */
} BenchVerb;
#endif /* CONFIG_ZUZU_BENCH */

/** @brief System call number type. */
typedef uint32_t Svc; 

#endif /* SYSCALL_NUMS_H */

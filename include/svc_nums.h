#ifndef SYSCALL_NUMS_H
#define SYSCALL_NUMS_H

#include <stdint.h>

typedef enum {
    SVC_QUIT = 0,
    SVC_YIELD,
    SVC_SLEEP,
    SVC_LOG,
    SVC_CREATE,
    SVC_CALL,
    SVC_REPLY,
    SVC_WAITON,
    SVC_MANAGEHANDLE,
    SVC_SIGNAL,
    SVC_BIND,
    SVC_MANAGEMEMORY,
    SVC_MANAGETASK,
#ifdef CONFIG_ZUZU_BENCH
    SVC_BENCH,
#endif
    SVC_TOTAL_COUNT
} SvcNumber;

#ifdef CONFIG_ZUZU_BENCH
typedef enum {
    BENCH_RESET,
    BENCH_DUMP,
} BenchVerb;
#endif

typedef uint8_t Svc;

#endif /* SYSCALL_NUMS_H */

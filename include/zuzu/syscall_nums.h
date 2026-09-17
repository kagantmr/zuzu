#ifndef SYSCALL_NUMS_H
#define SYSCALL_NUMS_H

#include <stdint.h>

typedef enum {
    SYS_QUIT = 0,
    SYS_YIELD,
    SYS_LOG,
    SYS_CREATE,
    SYS_CALL,
    SYS_REPLY,
    SYS_WAITON,
    SYS_CNTLHANDLE,
    SYS_SIGNAL,
    SYS_BINDEVENT,
    SYS_CNTLMEMORY,
    SYS_COMPLETEIRQ,
    SYSCALL_COUNT
} SyscallNumber;

typedef uint8_t Svc;

#endif /* SYSCALL_NUMS_H */

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
    SVC_TOTAL_COUNT
} SvcNumber;

typedef uint8_t Svc;

#endif /* SYSCALL_NUMS_H */

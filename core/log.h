#ifndef LOG_H
#define LOG_H

#include "kprintf.h"
#include "panic.h"

#define KINFO(fmt, ...)  kprintf("\033[36m[INFO] \033[0m" fmt "\n", ##__VA_ARGS__)
#define KWARN(fmt, ...)  kprintf("\033[33m[WARN] \033[0m" fmt "\n", ##__VA_ARGS__)
#define KERROR(fmt, ...) kprintf("\033[31m[ERR] \033[0m" fmt "\n", ##__VA_ARGS__)

// The do-while(0) loop ensures this works correctly inside if/else statements
#define KPANIC(fmt, ...) do { \
    kprintf("\033[1;31m[PANIC] \033[0m" fmt "\n", ##__VA_ARGS__); \
    panic(); \
} while(0)

#endif
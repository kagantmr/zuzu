#ifndef LOG_H
#define LOG_H

#include "kprintf.h"

#define KINFO(fmt, ...)  kprintf("\033[36m[INFO] \033[0m" fmt "\n", ##__VA_ARGS__)
#define KWARN(fmt, ...)  kprintf("\033[33m[WARN] \033[0m" fmt "\n", ##__VA_ARGS__)
#define KERROR(fmt, ...) kprintf("\033[31m[ERR] \033[0m" fmt "\n", ##__VA_ARGS__)
#define KPANIC(fmt, ...) kprintf("\033[1;31m[PANIC] \033[0m" fmt "\n", ##__VA_ARGS__)

#endif
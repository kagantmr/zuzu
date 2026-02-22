#ifndef LOG_H
#define LOG_H

#include "kprintf.h"
#include "kernel/time/tick.h"

#ifndef LOG_FMT
#define LOG_FMT(fmt) fmt
#endif

#define KINFO(fmt, ...)  kprintf("\033[36m[%6llu INFO ] \033[0m" LOG_FMT(fmt) "\n", get_ticks(), ##__VA_ARGS__)
#define KWARN(fmt, ...)  kprintf("\033[33m[%6llu WARN ] \033[0m" LOG_FMT(fmt) "\n", get_ticks(), ##__VA_ARGS__)
#define KERROR(fmt, ...) kprintf("\033[31m[%6llu ERR  ] \033[0m" LOG_FMT(fmt) "\n", get_ticks(), ##__VA_ARGS__)
#ifdef DEBUG_PRINT
#define KDEBUG(fmt, ...) kprintf("\033[35m[%6llu DEBUG] \033[0m" LOG_FMT(fmt) "\n", get_ticks(), ##__VA_ARGS__)
#else
#define KDEBUG(fmt, ...) do {} while(0)
#endif
#endif
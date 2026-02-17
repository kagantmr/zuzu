#ifndef LOG_H
#define LOG_H

#include "kprintf.h"
#include "panic.h"

#define KINFO(fmt, ...)  kprintf("\033[36m[INFO] \033[0m" fmt "\n", ##__VA_ARGS__)
#define KWARN(fmt, ...)  kprintf("\033[33m[WARN] \033[0m" fmt "\n", ##__VA_ARGS__)
#define KERROR(fmt, ...) kprintf("\033[31m[ERR] \033[0m" fmt "\n", ##__VA_ARGS__)
#ifdef DEBUG_PRINT 
#define KDEBUG(fmt, ...) kprintf("\033[35m[DEBUG] \033[0m" fmt "\n", ##__VA_ARGS__)
#else
#define KDEBUG(fmt, ...) do {} while(0)
#endif
#endif
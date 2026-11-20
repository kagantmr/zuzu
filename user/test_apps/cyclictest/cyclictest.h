#ifndef CYCLICTEST_H
#define CYCLICTEST_H

#include <arch/cycles.h>
#include <fs/fsd_client.h>
#include <stdbool.h>
#include <stdint.h>
#include <zuzu/zuzu.h>

#define PAGE 4096u
#define WORKER_STACK (32 * 1024)
#define MAX_WORKERS 64
#define MAX_PER_KIND 8
#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

typedef struct Worker Worker;

typedef void (*WorkerFn)(Worker *w);

struct Worker {
    WorkerFn fn;
    const char *kind;
    Handle task;
    uint8_t *stack;
    Handle port;
    volatile uint64_t ops;
    volatile Err err;
    volatile uint32_t done;
};

typedef struct {
    const char *name;
    const char *desc;
    const char *kinds;
} Load;

extern volatile uint32_t g_stop;
extern uint32_t g_khz;

const Load *LoadFind(const char *name);
const Load *LoadAt(uint32_t i);
uint32_t LoadCount(void);
uint32_t LoadStart(const Load *l, uint32_t per_kind, uint32_t prio, Worker *ws, uint32_t max);
void LoadStop(Worker *ws, uint32_t n);

int OutOpen(const char *path);
bool OutEnabled(void);
void OutSection(const char *name);
const char *OutSectionName(void);
void OutPrintf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void OutMeta(const char *key, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void OutFlush(void);
void OutClose(void);
FsdConn *FsConn(void);

#endif

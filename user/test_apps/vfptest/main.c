#include <zuzu/types.h>
#include <zuzu/task.h>
#include <zuzu/umem.h>
#include <stdio.h>
#include <stdbool.h>

/* Exercises the lazy VFP context switch: N threads each run a deterministic
 * double-precision recurrence, yielding after every step so the scheduler
 * interleaves them as much as possible. If ownership tracking, save, or
 * restore is wrong, one thread's FPU registers get clobbered by another's
 * and the result diverges from the single-threaded reference. */

#define STACK_SIZE 4096
#define ITERS 3000
#define NTHREADS 3

static double compute(double seed, int iters)
{
    double x = seed;
    for (int i = 0; i < iters; i++) {
        x = x * 1.0000003 + 0.0000001;
        if (i % 7 == 0)
            x = x - 0.0000002;
        ZuzuYield();
    }
    return x;
}

static double expected[NTHREADS];
static double got[NTHREADS];
static volatile int done[NTHREADS];

static const double seeds[NTHREADS] = { 1.5, -42.0, 0.000123 };

typedef struct { void *stack; int idx; } worker_arg_t;

static void worker(void *arg)
{
    worker_arg_t *w = (worker_arg_t *)arg;
    got[w->idx] = compute(seeds[w->idx], ITERS);
    done[w->idx] = 1;
    ZuzuTQuit(0);
}

int main(void)
{
    /* Reference values, computed serially before any other FPU-using
     * thread exists so this run can't be corrupted by anyone else. */
    for (int i = 0; i < NTHREADS; i++)
        expected[i] = compute(seeds[i], ITERS);

    Tid tids[NTHREADS];
    worker_arg_t args[NTHREADS];

    for (int i = 0; i < NTHREADS; i++) {
        void *stack = ZuzuMemMap(HANDLE_ANON, STACK_SIZE, PROT_READ | PROT_WRITE, 0);
        args[i] = (worker_arg_t){ .stack = stack, .idx = i };
        tids[i] = ZuzuTMake(worker, (char *)stack + STACK_SIZE, &args[i]);
    }

    for (int i = 0; i < NTHREADS; i++)
        ZuzuTJoin(tids[i]);

    int fails = 0;
    for (int i = 0; i < NTHREADS; i++) {
        bool ok = done[i] && got[i] == expected[i];
        printf("thread %d: expected=%.15g got=%.15g -> %s\n",
               i, expected[i], got[i], ok ? "ok" : "FAIL");
        if (!ok)
            fails++;
    }

    for (int i = 0; i < NTHREADS; i++)
        ZuzuMemUnmap(args[i].stack);

    printf("\n%s (%d failures)\n", fails ? "FAILED" : "ALL PASS", fails);
    return fails;
}

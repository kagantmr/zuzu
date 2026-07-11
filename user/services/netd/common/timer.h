#ifndef NETD_TIMER_H
#define NETD_TIMER_H

#include <zuzu/types.h>
#include <stdbool.h>

#define TIMER_MAX 64
#define TIMER_NONE (-1)
#define TIMER_NO_DEADLINE UINT32_MAX

typedef int timer_handle_t;

/* TODO: generation counter if reuse races appear */
typedef struct {
    bool      active;
    uint32_t  deadline_ms;          /* absolute, vs net_now_ms() */
    void    (*cb)(void *arg);
    void     *arg;
} timer_t;

void timer_init(void);
timer_handle_t timer_arm(uint32_t deadline_ms, void (*cb)(void *arg), void *arg);
void           timer_cancel(timer_handle_t h);
uint32_t timer_next_deadline(void);
void     timer_run_expired(void);  

#endif
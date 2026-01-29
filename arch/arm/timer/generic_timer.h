#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>

void generic_timer_init(void);
uint64_t generic_timer_get_ticks(void);

#endif // TIMER_H
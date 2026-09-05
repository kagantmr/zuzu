#ifndef ONCE_KERNEL_TIME_TICK_H
#define ONCE_KERNEL_TIME_TICK_H

#include <stdint.h>

#define TICK_HZ 100 // 100 ticks per second

#ifdef TIME_MEASURE
extern uint32_t ctx_switch_start;
extern uint32_t ctx_switch_cost;
#endif

// Get current tick count (monotonic, starts at 0)
uint64_t GetTicks(void);

// Get ticks per second (HZ)
uint32_t GetTickRate(void);

// Register callback called on each tick (for scheduler)
typedef void (*TickCb)(void);
void RegisterTickCb(TickCb cb);
void tick_announce(void);

#endif // ONCE_KERNEL_TIME_TICK_H
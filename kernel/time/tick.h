#ifndef ONCE_KERNEL_TIME_TICK_H
#define ONCE_KERNEL_TIME_TICK_H

#include <stdint.h>

#define TICK_HZ 100  // 100 ticks per second

// Get current tick count (monotonic, starts at 0)
uint64_t get_ticks(void);

// Get ticks per second (HZ)
uint32_t get_tick_rate(void);

// Register callback called on each tick (for scheduler)
typedef void (*tick_callback_t)(void);
void register_tick_callback(tick_callback_t cb);
void tick_announce(void);

#endif // ONCE_KERNEL_TIME_TICK_H
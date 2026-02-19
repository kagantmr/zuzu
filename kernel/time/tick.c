#include "tick.h"
#include "core/log.h"
#include "kernel/stats/stats.h"
#include <stddef.h>


static volatile uint64_t tick_count = 0;
static tick_callback_t tick_callback = NULL;

uint64_t get_ticks(void) {
    return tick_count;
}

uint32_t get_tick_rate(void) {
    return TICK_HZ;
};

void register_tick_callback(tick_callback_t cb) {
    tick_callback = cb;
};

void tick_announce(void) {
    tick_count++;
    stats_check_input();
    if (tick_callback) {
        tick_callback();
    }
    if (tick_count % TICK_HZ == 0) {
        /**
         * This section is reserved for time-based tests. 
         * The compiler will likely optimize it away if not filled.
         */
        //extern pmm_state_t pmm_state; KINFO("PMM: %d free pages", pmm_state.free_pages);
        //panic("Zuzu attempted to chew on the wires");
        //uint32_t lo = (uint32_t)tick_count;
        //uint32_t hi = (uint32_t)(tick_count >> 32);
        // KINFO("Tick: %llu", tick_count);
    }
}
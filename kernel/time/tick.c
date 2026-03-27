#include "tick.h"
#include "core/log.h"
#include <stddef.h>


static volatile uint64_t tick_count = 0;
static tick_callback_t tick_callback = NULL;
extern void syspage_update_uptime(void);

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
    syspage_update_uptime(); // update uptime in syspage on every tick
    if (tick_callback) {
        tick_callback();
    }
}
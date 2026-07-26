#include "tick.h"
#include "core/log.h"
#include <stddef.h>

static volatile uint64_t tick_count = 0;
static tick_callback_t tick_callback = NULL;
extern void syspage_update_uptime(void);

#ifdef CTX_SWITCH_MEASURE
uint32_t ctx_switch_start = 0;
uint32_t ctx_switch_cost = 0;
#endif

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
#ifdef CTX_SWITCH_MEASURE
    if (tick_count % 1000 == 0) {
        KDEBUG("Context switch start: %u, cost: %u", ctx_switch_start, ctx_switch_cost);
    }
#endif
}
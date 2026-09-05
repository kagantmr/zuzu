#include "tick.h"
#include "compiler.h"
#include <arch/timer.h>
#include <stddef.h>
#include <stdint.h>
#include <zuzu/types.h>

static volatile Tick tick_count = 0;
static TickCb tick_callback = NULL;
extern void SyspageUpdateUptime(void);

#ifdef TIME_MEASURE
uint32_t ctx_switch_start = 0;
uint32_t ctx_switch_cost = 0;
#endif

static uint32_t f = 0;


Tick GetTicks(void)
{
    uint32_t archf = ArchTimerFreq();
    if (unlikely(archf == 0)) {
        return 0;                 // timer not brought up yet
    }
    if (unlikely(f == 0)) {
        f = (archf / TICK_HZ);
    }
    return ArchTimerNow() / f;
}

uint32_t GetTickRate(void) { return TICK_HZ; };

void RegisterTickCb(TickCb cb) { tick_callback = cb; };

void tick_announce(void)
{
    tick_count++;
    SyspageUpdateUptime(); // update uptime in syspage on every tick
    if (tick_callback)
    {
        tick_callback();
    }
#ifdef TIME_MEASURE
    if (tick_count % 1000 == 0)
    {
        KDEBUG("Context switch start: %u, cost: %u", ctx_switch_start, ctx_switch_cost);
    }
#endif
}

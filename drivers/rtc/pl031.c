#include "drivers/driver.h"
#include "kernel/mm/vmm/vmm.h"
#include <stdint.h>

#define LOG_FMT(fmt) "(board) " fmt
#include "core/log.h"

extern uint32_t rtc_epoch;

static void Pl031Probe(const FdtDevice *dev)
{
	void *va = IoRemap((uintptr_t)dev->phys, (size_t)dev->size);
	if (!va)
		return;

	rtc_epoch = *((volatile uint32_t *)va);
	if (rtc_epoch == 0)
		KDEBUG("Oops! RTC epoch is 0 (check for anomalies)");
	IoUnmap(va);
}

static const char *const PL031_COMPAT[] = { "arm,pl031", NULL };

ZUZU_DRIVER(pl031, ZUZU_DRV_MISC) = {
	.name = "PL031 RTC", .compat = PL031_COMPAT, .required = false, .probe = Pl031Probe,
};

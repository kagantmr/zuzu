#ifndef ZUZU_DRIVER_H
#define ZUZU_DRIVER_H

#include "kernel/dev/fdt_wrappers.h"
#include <stdbool.h>

/* Probe order. The console must come up before the irqchip: the GIC probe
 * logs, and logging needs a console. */
#define ZUZU_DRV_CONSOLE "00"
#define ZUZU_DRV_IRQCHIP "01"
#define ZUZU_DRV_TIMER	 "02"
#define ZUZU_DRV_MISC	 "03"

typedef struct ZuzuDriver {
	const char *name;
	const char *const *compat; /* NULL-terminated; NULL = no DTB match needed */
	bool required;		   /* absent device panics rather than logs */
	void (*probe)(const FdtDevice *dev);
} ZuzuDriver;

/* 'used' keeps the compiler from dropping it; KEEP() in the linker script
 * keeps --gc-sections from dropping the section. ('retain' is accepted by
 * __has_attribute here but ignored by the toolchain, so it is not used.) */
#define ZUZU_DRV_KEEP __attribute__((used))

#define ZUZU_DRIVER(sym, prio)                                                                     \
	static const ZuzuDriver __zuzu_drv_##sym ZUZU_DRV_KEEP                                     \
	    __attribute__((section(".zuzu_drivers." prio), aligned(4)))

/* Typed, so the walk needs no cast that -Wcast-align/-Wcast-qual would reject. */
extern const ZuzuDriver __zuzu_drivers_start[];
extern const ZuzuDriver __zuzu_drivers_end[];

void DriverProbeAll(void);

#endif /* ZUZU_DRIVER_H */

#include "drivers/driver.h"
#include "kernel/boot_info.h"
#include "core/panic.h"

#define LOG_FMT(fmt) "(board) " fmt
#include "core/log.h"

void DriverProbeAll(void)
{
	for (const ZuzuDriver *d = __zuzu_drivers_start; d < __zuzu_drivers_end; d++) {
		const FdtDevice *dev = NULL;

		if (d->compat) {
			dev = boot_info_find_compatible(d->compat);
			if (!dev) {
				if (d->required)
					panic("%s not found in DTB", d->name);
				KWARN("no %s on this board", d->name);
				continue;
			}
		}

		d->probe(dev);
	}
}


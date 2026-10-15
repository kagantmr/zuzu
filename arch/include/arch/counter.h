// arch/counter.h - Neutral user-readable monotonic counter contract.
//
//   uint64_t ArchCounterRead(void);
//       The counter the kernel schedules with (ArchTimerNow()), readable from
//       user mode. Monotonic, never wraps in practice. Its frequency and the
//       value at boot are published in the syspage (counter_freq,
//       counter_boot); use <zuzu/uptime.h> rather than calling this directly.

#ifndef ARCH_COUNTER_H
#define ARCH_COUNTER_H

#include <stdint.h>
#include <arch_impl/counter.h>

#endif // ARCH_COUNTER_H

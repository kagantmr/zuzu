// arch/cycles.h - Neutral cycle-counter contract (benchmarking only).
//
//   uint32_t ArchMeasure(void);
//       Free-running high-resolution counter, read with ordering barriers on
//       both sides so two reads bracket exactly the code between them.
//       Unit is architecture-defined (CPU cycles on ARM, TSC ticks on x86);
//       compare numbers only within one machine.
//       Truncated to 32 bits and wraps: only (end - start) in uint32_t
//       arithmetic over intervals shorter than 2^32 units is meaningful.
//       Readable from kernel and user mode once arch early boot has enabled
//       it. An arch that cannot provide it returns 0.
//
// Not a clock: use ArchTimerNow() for time.

#ifndef ARCH_CYCLES_H
#define ARCH_CYCLES_H

#include <stdint.h>
#include <arch_impl/cycles.h>

#endif // ARCH_CYCLES_H

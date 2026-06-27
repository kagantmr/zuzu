// arch/timer.h - Neutral periodic-timer contract.
//
//   void arch_timer_init(void);  -- start the periodic tick that drives
//                                   the scheduler via the kernel tick subsystem.

#ifndef ZUZU_ARCH_TIMER_H
#define ZUZU_ARCH_TIMER_H

void arch_timer_init(void);

#endif // ZUZU_ARCH_TIMER_H

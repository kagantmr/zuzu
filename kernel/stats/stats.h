#ifndef KERNEL_STATS_STATS_H
#define KERNEL_STATS_STATS_H

#ifdef STATS_MODE

#include <stdbool.h>

extern volatile bool stats_mode_active;

void stats_check_input(void);

#else

#define stats_mode_active false
static inline void stats_check_input(void) {}

#endif /* STATS_MODE */

#endif /* KERNEL_STATS_STATS_H */

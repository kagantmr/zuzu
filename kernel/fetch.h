#pragma once

#ifndef ZUZU_BANNER_DISABLE
void print_boot_banner(void);
#else
static inline void print_boot_banner(void) {}
#endif
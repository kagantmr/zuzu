#ifndef ROOTSVC_H
#define ROOTSVC_H

#include <zuzu/bootinfo.h>

/* Set once by main() before any service thread is spawned. */
extern const BootInfo *g_bootinfo;

void DevsvcMain(void);
void NsvcMain(void);

#endif

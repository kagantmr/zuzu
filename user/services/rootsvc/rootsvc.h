#ifndef ROOTSVC_H
#define ROOTSVC_H

#include <zuzu/bootinfo.h>
#include <types.h>

/* Set once by main() before any service thread is spawned. */
extern const BootInfo *g_bootinfo;
extern Handle g_nsvc_port;

void DevsvcMain(void);
void NsvcMain(void);
Handle NsvcInit(void);


#endif

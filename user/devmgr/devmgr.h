#ifndef DEVMGR_H
#define DEVMGR_H

#define DEVMGR_VER "v1.0"

#include "zuzu.h"
#include <zuzu/protocols/devmgr_protocol.h>
#include <zuzu/protocols/nt_protocol.h>

#define DEVMGR_NAME "devm"

int devmgr_setup(void);
void devmgr_loop(int32_t port_handle);

#endif
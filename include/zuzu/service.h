#ifndef ZUZU_SERVICE_H
#define ZUZU_SERVICE_H

#include "types.h"
#ifdef __cplusplus
extern "C" {
#endif

#define NSVC_PORT 0
#define NS_MAX_PATH 64
#define NS_MAX_SERVICES 512

typedef enum {
    NS_REGISTER = 1,
    NS_LOOKUP
} NsvcOpcode;

typedef struct {
    NsvcOpcode cmd;
    Handle handle;
    Spid pid;
    char *path;
} NsvcRequest;

Err RegisterService(const char *name, Handle port);

Handle LookupService(const char *name);

#ifdef __cplusplus
}
#endif

#endif

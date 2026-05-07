#ifndef DEN_H
#define DEN_H

#include <zuzu/types.h>

den_id_t den_create(const char *name, uint32_t cap);
int den_destroy(den_id_t id);
den_id_t den_myden(const char *name);

#endif // DEN_H
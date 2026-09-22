#ifndef _ZUZU_OBJECTS_SPACE_H
#define _ZUZU_OBJECTS_SPACE_H

#include "kernel/mm/vmm.h"
#include "handle.h"

typedef struct SpaceObjectStruct {
    AddressSpace *as;
    HandleTable handle_table;
} SpaceObject;

#endif /* _ZUZU_OBJECTS_SPACE_H */
    
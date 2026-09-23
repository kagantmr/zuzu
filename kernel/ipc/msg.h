#ifndef _ZUZU_KERNEL_IPC_MSG_H
#define _ZUZU_KERNEL_IPC_MSG_H

#include "kernel/task/task.h"
#include "kernel/space/handle.h"


void MsgBufCopy(TaskObject *restrict src, TaskObject *restrict dst, size_t len);

HandleTableEntry __hot *ValidateCallPort(SpaceObject *space, Handle handle, CpuState *frame);
Handle __hot GrantHandleAcross(SpaceObject *from, SpaceObject *to,
                                  Handle handle_to_grant, CpuState *frame);
void __hot CallBlockAsSender(TaskObject *caller, PortObject *port,
                               HandleTableEntry *entry, EphemeralReplyObject *rc,
                               uint32_t xlen, Handle grant_handle);
bool __hot CallHandoffToReceiver(TaskObject *caller, PortObject *port,
                                   EphemeralReplyObject *rc, size_t xlen, Handle grant_handle,
                                   CpuState *frame);


void __hot ReplyDeliverToCaller(TaskObject *target, uint32_t xlen, Handle granted);

#endif /* _ZUZU_KERNEL_IPC_MSG_H */
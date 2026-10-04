#ifndef _ZUZU_KERNEL_IPC_MSG_H
#define _ZUZU_KERNEL_IPC_MSG_H

#include "kernel/task/task.h"
#include "kernel/space/handle.h"


void MsgBufCopy(TaskObject *restrict src, TaskObject *restrict dst, size_t len);

HandleTableEntry __hot *ValidateCallPort(SpaceObject *space, Handle handle, CpuState *frame);

Err ValidateGrantHandle(SpaceObject *from, Handle handle_to_grant);


Err AllocateGrantSlot(SpaceObject *from, SpaceObject *to, Handle handle_to_grant, Handle *out);

Err GrantHandleAcross(SpaceObject *from, SpaceObject *to, Handle handle_to_grant, Handle *out);

void __hot CallBlockAsSender(TaskObject *caller, PortObject *port,
                               ReplyObject *rc,
                               uint32_t xlen, Handle grant_handle);
void __hot DeliverCallToReceiver(TaskObject *caller, TaskObject *rx, ReplyObject *rc,
                                   size_t xlen, Handle granted);
bool __hot CallHandoffToReceiver(TaskObject *caller, PortObject *port,
                                   ReplyObject *rc, size_t xlen, Handle grant_handle,
                                   CpuState *frame);

void ReplyFailCaller(TaskObject *target, Err err);

void __hot ReplyDeliverToCaller(TaskObject *target, uint32_t xlen, Handle granted);

void PortReceive(PortObject *port, Duration timeout, CpuState *frame);

#endif /* _ZUZU_KERNEL_IPC_MSG_H */
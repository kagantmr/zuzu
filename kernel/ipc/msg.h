#ifndef _ZUZU_KERNEL_IPC_MSG_H
#define _ZUZU_KERNEL_IPC_MSG_H

#include "kernel/task/task.h"
#include "kernel/space/handle.h"


void MsgBufCopy(TaskObject *restrict src, TaskObject *restrict dst, size_t len);

HandleTableEntry __hot *ValidateCallPort(SpaceObject *space, Handle handle, CpuState *frame);

/* Validate-only: does the source handle exist and is it grantable? Safe to
 * call before a Call blocks, when the destination table isn't known yet. */
Err ValidateGrantHandle(SpaceObject *from, Handle handle_to_grant);

/* Allocation-only: assumes handle_to_grant already passed ValidateGrantHandle.
 * *out is set to -1 if handle_to_grant is -1 (no grant requested). */
Err AllocateGrantSlot(SpaceObject *from, SpaceObject *to, Handle handle_to_grant, Handle *out);

/* Validate + allocate in one call, for call sites (SvcReply) that know the
 * destination table up front. *out is -1 on no-grant or on failure. */
Err GrantHandleAcross(SpaceObject *from, SpaceObject *to, Handle handle_to_grant, Handle *out);

void __hot CallBlockAsSender(TaskObject *caller, PortObject *port,
                               EphemeralReplyObject *rc,
                               uint32_t xlen, Handle grant_handle);
void __hot DeliverCallToReceiver(TaskObject *caller, TaskObject *rx, EphemeralReplyObject *rc,
                                   size_t xlen, Handle granted);
bool __hot CallHandoffToReceiver(TaskObject *caller, PortObject *port,
                                   EphemeralReplyObject *rc, size_t xlen, Handle grant_handle,
                                   CpuState *frame);


void __hot ReplyDeliverToCaller(TaskObject *target, uint32_t xlen, Handle granted);

#endif /* _ZUZU_KERNEL_IPC_MSG_H */
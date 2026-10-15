#ifndef KERNEL_IPC_MSG_H
#define KERNEL_IPC_MSG_H

#include "kernel/space/handle.h"
#include "kernel/task/task.h"

#define TICKET_PACK(index, slot, gen)                                                              \
    (((uint32_t)(gen) << 16) | ((uint32_t)(slot) << 10) | ((uint32_t)(index) & HANDLE_INDEX_MASK))
#define TICKET_INDEX(t) ((uint32_t)(t) & HANDLE_INDEX_MASK)
#define TICKET_SLOT(t) (((uint32_t)(t) >> 10) & 0x3Fu)
#define TICKET_GEN(t) (((uint32_t)(t) >> 16) & 0x7FFFu)

Err PortHoldCurrent(TaskObject *server, Ticket *out);

TaskObject *PortTakeHeld(TaskObject *server, Ticket ticket, Err *err);

void MsgBufCopy(TaskObject *restrict src, TaskObject *restrict dst, size_t len);

HandleTableEntry __hot *ValidateCallPort(SpaceObject *space, Handle handle, CpuState *frame);

Err ValidateGrantHandle(SpaceObject *from, Handle handle_to_grant);

Err AllocateGrantSlot(SpaceObject *from, SpaceObject *to, Handle handle_to_grant, Handle *out);

Err GrantHandleAcross(SpaceObject *from, SpaceObject *to, Handle handle_to_grant, Handle *out);

void __hot CallBlockAsSender(TaskObject *caller, PortObject *port, ReplyObject *rc, uint32_t xlen,
                             Handle grant_handle);
void __hot DeliverCallToReceiver(TaskObject *caller, TaskObject *rx, ReplyObject *rc, size_t xlen,
                                 Handle granted);
bool __hot CallHandoffToReceiver(TaskObject *caller, PortObject *port, ReplyObject *rc, size_t xlen,
                                 Handle grant_handle, CpuState *frame);

void ReplyFailCaller(TaskObject *target, Err err);

void __hot ReplyDeliverToCaller(TaskObject *target, uint32_t xlen, Handle granted);

void PortReceive(PortObject *port, Duration timeout, CpuState *frame);

#endif /* KERNEL_IPC_MSG_H */
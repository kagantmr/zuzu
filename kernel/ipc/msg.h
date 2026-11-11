/**
 * @file msg.h
 * @brief Message-passing is the heart of zuzu just like any other microkernel. zuzu has synchronous
 * rendezvous message-passing via the per-task message buffers that zuzu copies up to 512 bytes of
 * the message to.
 */

#ifndef KERNEL_IPC_MSG_H
#define KERNEL_IPC_MSG_H

#include "kernel/space/handle.h"
#include "kernel/task/task.h"

#if UINTPTR_MAX > 0xFFFFFFFFu
/** @brief Bits of the held slot's generation a ticket carries. */
#define TICKET_GEN_BITS 15u
/** @brief Bits of the port handle's generation a ticket carries: all of them. */
#define TICKET_HGEN_BITS HANDLE_GEN_BITS
#else
/** @brief Bits of the held slot's generation a ticket carries. */
#define TICKET_GEN_BITS 8u
/** @brief Bits of the port handle's generation a ticket carries: only the low ones on 32 bits. */
#define TICKET_HGEN_BITS 7u
#endif
/** @brief Mask of the held slot's generation in a ticket. */
#define TICKET_GEN_MASK ((1u << TICKET_GEN_BITS) - 1u)
/** @brief Mask of the port handle's generation in a ticket. */
#define TICKET_HGEN_MASK ((1u << TICKET_HGEN_BITS) - 1u)
/** @brief First bit of the port handle's generation in a ticket. */
#define TICKET_HGEN_SHIFT (16u + TICKET_GEN_BITS)

/**
 * @brief Builds a Hold() ticket.
 *
 * Layout, from bit 0: the handle index of the port the call was received through
 * (bits 0-9), the slot in that port's held table (bits 10-15), the slot's generation
 * (TICKET_GEN_BITS from bit 16), then the generation of the port handle
 * (TICKET_HGEN_BITS). On AArch64 the whole handle generation fits; on ARMv7 only its
 * low 7 bits do, and the slot generation is 8 bits, so a ticket kept across 128
 * reuses of its port's handle slot or 255 holds on its held slot can match again.
 * The top bit stays clear.
 *
 * @param[in] index Handle index of the port in the server's space.
 * @param[in] slot  Slot in the port's held table, below PORT_HELD_SLOTS.
 * @param[in] gen   Generation of the slot, 1 to TICKET_GEN_MASK.
 * @param[in] hgen  Generation of the port handle.
 */
#define TICKET_PACK(index, slot, gen, hgen)                                                        \
    ((((Ticket)(hgen) & TICKET_HGEN_MASK) << TICKET_HGEN_SHIFT) |                                  \
     ((Ticket)((gen) & TICKET_GEN_MASK) << 16) | ((Ticket)(slot) << 10) |                          \
     ((Ticket)(index) & HANDLE_INDEX_MASK))
/** @brief Handle index of the port a ticket was issued on. */
#define TICKET_INDEX(t) ((uint32_t)(t) & HANDLE_INDEX_MASK)
/** @brief Slot in the port's held table that a ticket names. */
#define TICKET_SLOT(t) (((uint32_t)(t) >> 10) & 0x3Fu)
/** @brief Generation a ticket expects its slot to have; a mismatch means a stale or forged one. */
#define TICKET_GEN(t) ((uint32_t)((t) >> 16) & TICKET_GEN_MASK)
/** @brief Port handle generation (or its low bits) a ticket was issued under. */
#define TICKET_HGEN(t) ((uint32_t)((t) >> TICKET_HGEN_SHIFT) & TICKET_HGEN_MASK)

/**
 * @brief Put the current incoming call of @p server on hold, and emit a ticket to @p out
 *
 * @param[in] server The server that wants to hold the call.
 * @param[out] out The ticket of the hold.
 *
 * @return Err
 * @retval ERR_BADARG The arguments are NULL.
 * @retval ERR_NOMEM The port has reached its maximum hold capacity.
 * @retval ERR_DEAD The port's owner space has died.
 */
Err PortHoldCurrent(TaskObject *server, Ticket *out);

/**
 * @brief Removes a caller from on hold and into an incoming call.
 *
 * @param[in] server The task that wants to answer a held call.
 * @param[in] ticket The ticket of the held call.
 * @param[out] err Error code.
 *
 * @return TaskObject* Pointer to the unheld caller.
 */
TaskObject *PortTakeHeld(TaskObject *server, Ticket ticket, Err *err);

/**
 * @brief Copies a message from one message buffer to another.
 *
 * @param[in] src The task object to take the message from.
 * @param[in] dst The task object to deliver the message to
 * @param[in] len The length of the message (maximum 512).
 */
void MsgBufCopy(TaskObject *restrict src, TaskObject *restrict dst, size_t len);

/**
 * @brief Series of ENSURE statements to validate the port is safe to call on.
 *
 * Fills the frame and returns NULL if the checks fail.
 *
 * @param[in] space The caller space.
 * @param[in] handle The handle to verify.
 * @param[in,out] frame The task's CPU frame.
 *
 * @return HandleTableEntry* The validated port entry.
 */
HandleTableEntry __hot *ValidateCallPort(SpaceObject *space, Handle handle, CpuState *frame);

/**
 * @brief Validate a grant by checking permissions and object validity.
 *
 * @param[in] from The granter.
 * @param[in] handle_to_grant The handle to the object being granted.
 *
 * @return Err
 * @retval ERR_NOPERM Lacking PERM_TXFR.
 * @retval ERR_BADHANDLE The handle is invalid.
 */
Err ValidateGrantHandle(SpaceObject *from, Handle handle_to_grant);

/**
 * @brief Start a grant by validating, then calling HandleCopyInto()
 *
 * @param[in] from The granter.
 * @param[in] to The grantee.
 * @param[in] handle_to_grant The handle to the object being granted.
 * @param[out] out Granted object's handle in the grantee's table
 *
 * @return Err
 * @retval ERR_NOPERM Lacking PERM_TXFR.
 * @retval ERR_BADHANDLE The handle is invalid.
 */
Err AllocateGrantSlot(SpaceObject *from, SpaceObject *to, Handle handle_to_grant, Handle *out);

/**
 * @brief Grant a handle across. Top level grant function.
 *
 * @param[in] from The granter.
 * @param[in] to The grantee.
 * @param[in] handle_to_grant The handle to the object being granted.
 * @param[out] out Granted object's handle in the grantee's table
 *
 * @return Err
 * @retval ERR_NOPERM Lacking PERM_TXFR.
 * @retval ERR_BADHANDLE The handle is invalid.
 */
Err GrantHandleAcross(SpaceObject *from, SpaceObject *to, Handle handle_to_grant, Handle *out);

/**
 * @brief If a caller has nobody waiting, then block on the port.
 * 
 * @param[in] caller The calling task.
 * @param[in] port The port to block on.
 * @param[in] rc The reply object to set.
 * @param[in] xlen The message length (max 512).
 * @param[in] grant_handle The handle to grant across.
 * @param[in] timeout The timeout of the call.
 */
void __hot CallBlockAsSender(TaskObject *caller, PortObject *port, ReplyObject *rc, uint32_t xlen,
                             Handle grant_handle, Duration timeout);

/**
 * @brief Boost the priority of the callee task, and copy the message.
 * 
 * @param[in] caller The calling task.
 * @param[in] rx The port to deliver on.
 * @param[in] rc The reply object to serve.
 * @param[in] xlen The message length (max 512).
 * @param[in] granted The handle to grant across.
 */
void __hot DeliverCallToReceiver(TaskObject *caller, TaskObject *rx, ReplyObject *rc, size_t xlen,
                                 Handle granted);

/**
 * @brief Directly switch to the callee task and deliver the message.
 * 
 * @param[in] caller The calling task.
 * @param[in] port The port to deliver on.
 * @param[in] rc The reply object to set.
 * @param[in] xlen The message length (max 512).
 * @param[in] grant_handle The handle to grant across.
 * @param[in] timeout The timeout of the call.
 * @param[out] frame The CPU frame to fill.
 * 
 * @return bool
 * @retval true Success.
 * @retval false Failure. Frame's r0 will tell why.
 */
bool __hot CallHandoffToReceiver(TaskObject *caller, PortObject *port, ReplyObject *rc, size_t xlen,
                                 Handle grant_handle, Duration timeout, CpuState *frame);

/**
 * @brief Fail the call with an error.
 * 
 * @param[in] target The task to deliver failure.
 * @param[in] err The error to deliver.
 */
void ReplyFailCaller(TaskObject *target, Err err);

/**
 * @brief Deliver the reply to the caller.
 * 
 * @param[in] target The task to deliver failure.
 * @param[in] xlen The message length (max 512).
 * @param[in] granted The handle to grant across.
 */
void __hot ReplyDeliverToCaller(TaskObject *target, size_t xlen, Handle granted);

/**
 * @brief Block on this port, not waken until a message arrives.
 * 
 * @param[in] port The port to block on.
 * @param[in] timeout Maximum blocking time.
 * @param[out] frame The CPU frame to fill on errors.
 */
void PortReceive(PortObject *port, Duration timeout, CpuState *frame);

#endif /* KERNEL_IPC_MSG_H */

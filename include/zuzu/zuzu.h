#ifndef _LIB_ZUZU_H
#define _LIB_ZUZU_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <arch/svc.h>
#include <arch/regs.h>
#include <stdint.h>
#include "svc_nums.h"
#include <string.h>
#include <types.h>

/**
    * @brief Terminates the calling task with the specified exit status.
    *
    * @param exit_status The exit status to return to the caller.
    *
    * @return Never returns.
    */
static inline void Quit(Err exit_status) { ArchInvokeSvc(SVC_QUIT, exit_status, 0, 0, 0); }

/**
* @brief Yields the calling task voluntarily, allowing other tasks to run.
*/
static inline void Yield(void) { ArchInvokeSvc(SVC_YIELD, 0, 0, 0, 0); }

/**
* @brief Suspends the calling process for a specified number of milliseconds.
*
* @param ms The number of milliseconds to sleep.
*
* @return ZUZU_OK unconditionally.
*/
static inline Err Sleep(Duration ms) { return ArchInvokeSvc(SVC_SLEEP, (Register)ms, 0, 0, 0); }

/**
 * @brief Creates a task object in the specified space.
 *
 * @param space_handle The handle of the space to create the task in.
 *
 * @return The handle of the created task.
 */
static inline Handle CreateTask(Handle space_handle) {
    return ArchInvokeSvc(SVC_CREATE, OBJECT_TASK, space_handle, 0, 0);
}

/**
 * @brief Creates a space object with the specified name.
 *
 * @param name The name of the space to create.
 *
 * @return The handle of the created space.
 */
static inline Handle CreateSpace(const char *name) {
    return ArchInvokeSvc(SVC_CREATE, OBJECT_SPACE, (VirtAddr)name, strlen(name), 0);
}

/**
 * @brief Creates a port object.
 *
 * @return The handle of the created port.
 */
static inline Handle CreatePort(void) {
    return ArchInvokeSvc(SVC_CREATE, OBJECT_PORT, 0, 0, 0);
}

/**
 * @brief Creates an event object.
 *
 * @return The handle of the created event.
 */
static inline Handle CreateEvent(void) {
    return ArchInvokeSvc(SVC_CREATE, OBJECT_EVENT, 0, 0, 0);
}

/**
 * @brief Creates a memory object with the specified page count.
 *
 * @param page_count The number of pages to allocate.
 *
 * @return The handle of the created memory.
 */
static inline Handle CreateMem(size_t page_count) {
    return ArchInvokeSvc(SVC_CREATE, OBJECT_MEMORY, (Register)page_count, 0, 0);
}

/**
 * @brief Starts a task with the specified handle, entry point, stack pointer, and initial register values.
 *
 * @param h The handle of the task to start.
 * @param entry The entry point of the task.
 * @param sp The stack pointer of the task.
 * @param r0 The initial value of register r0.
 * @param r1 The initial value of register r1.
 *
 * @return ZUZU_OK if the task was started successfully, error code otherwise.
 */
static inline Err TaskStart(Handle h, VirtAddr entry, VirtAddr sp, uint32_t r0, uint32_t r1) {
    KickstartArgs args = {
        .entry = entry,
        .sp = sp,
        .r0 = r0,
        .r1 = r1,
    };
    return ArchInvokeSvc(SVC_MANAGETASK, h, MNGTASK_START, (Register)(VirtAddr)&args, 0);
}

static inline Err TaskKill(Handle h) {
    return ArchInvokeSvc(SVC_MANAGETASK, h, MNGTASK_KILL, 0, 0);
}

static inline Err TaskSetPriority(Handle h, uint32_t val) {
    return ArchInvokeSvc(SVC_MANAGETASK, h, MNGTASK_SET_PRIORITY, (Register)val, 0);
}

static inline Err TaskSetMaxPriority(Handle h, uint32_t val) {
    return ArchInvokeSvc(SVC_MANAGETASK, h, MNGTASK_SET_MAX_PRIO, (Register)val, 0);
}

static inline Err TaskSetTimeSlice(Handle h, uint32_t val) {
    return ArchInvokeSvc(SVC_MANAGETASK, h, MNGTASK_SET_TIMESLICE, (Register)val, 0);
}

static inline Err TaskSuspend(Handle h) {
    return ArchInvokeSvc(SVC_MANAGETASK, h, MNGTASK_SUSPEND, 0, 0);
}

static inline Err TaskResume(Handle h) {
    return ArchInvokeSvc(SVC_MANAGETASK, h, MNGTASK_RESUME, 0, 0);
}

static inline Err TaskGetRegs(Handle h, Register out[ARCH_NUM_GP_REGS]) {
    return ArchInvokeSvc(SVC_MANAGETASK, h, MNGTASK_GET_REGS, (Register)(VirtAddr)out, 0);
}

static inline Err TaskSetRegs(Handle h, const Register in[ARCH_NUM_GP_REGS]) {
    return ArchInvokeSvc(SVC_MANAGETASK, h, MNGTASK_SET_REGS, (Register)(VirtAddr)in, 0);
}

/**
 * @brief Blocks on any waitable handle (Port, Event, Task, Space) until
 * something arrives or timeout elapses. What r1-r3 mean depends entirely
 * on the handle's type:
 *   Port:  r1 = sender's port marker, r2 = xlen, r3 = granted handle (or -1)
 *   Event: r1 = signaled bits
 *   Task:  r1 = exit_status (ZOMBIE) or WAKE_FAULT (FAULTED), r2 = fault reason if FAULTED
 *   Space: r1 = last_exit_status
 * r0 is always the status (ZUZU_OK or an Err).
 */
static inline SvcResult WaitOn(Handle h, Duration timeout) {
    return ArchInvokeSvc4(SVC_WAITON, h, (Register)timeout, 0, 0);
}

static inline PortWaitResult FormatToPortWait(SvcResult r) {
    return (PortWaitResult){
        .status = (Err)r.r0,
        .sender = (Marker)r.r1,
        .xlen = (uint32_t)r.r2,
        .granted = (Handle)r.r3,
    };
}

static inline EventWaitResult FormatToEventWait(SvcResult r) {
    return (EventWaitResult){
        .status = (Err)r.r0,
        .bits = (EventWord)r.r1,
    };
}

/** @brief .value is exit_status when .outcome == TASK_EXITED, or
 * fault_reason when .outcome == TASK_FAULTED. */
static inline TaskWaitResult FormatToTaskWait(SvcResult r) {
    return (TaskWaitResult){
        .status = (Err)r.r0,
        .outcome = (TaskWaitOutcome)r.r3,
        .value = (int32_t)r.r1,
    };
}

static inline SpaceWaitResult FormatToSpaceWait(SvcResult r) {
    return (SpaceWaitResult){
        .status = (Err)r.r0,
        .exit_status = (Err)r.r1,
    };
}

static inline SvcResult HandleDuplicate(Handle h, HandlePerms perms, Marker marker) {
    return ArchInvokeSvc4(SVC_MANAGEHANDLE, h, MNGHNDL_DUPLICATE, perms, (Register)marker);
}

static inline Err HandleRestrict(Handle h, HandlePerms mask) {
    return ArchInvokeSvc(SVC_MANAGEHANDLE, h, MNGHNDL_RESTRICT, mask, 0);
}

static inline Err HandleClose(Handle h) {
    return ArchInvokeSvc(SVC_MANAGEHANDLE, h, MNGHNDL_CLOSE, 0, 0);
}

static inline Err HandleDestroy(Handle h) {
    return ArchInvokeSvc(SVC_MANAGEHANDLE, h, MNGHNDL_DESTROY, 0, 0);
}

static inline SvcResult HandleQuery(Handle h, QueryWhat what) {
    return ArchInvokeSvc4(SVC_MANAGEHANDLE, h, MNGHNDL_QUERY, what, 0);
}

static inline VirtAddr MemMap(Handle mem_handle, VirtAddr hint_va, MemProt prot) {
    uint32_t packed = (hint_va & ~0xFFFU) | ((uint32_t)prot & 0x7U);
    return (VirtAddr)ArchInvokeSvc(SVC_MANAGEMEMORY, MNGMEM_MAP, mem_handle, 0, (Register)packed);}


static inline VirtAddr MemMapAnon(size_t size, VirtAddr hint_va, MemProt prot) {
    uint32_t packed = (hint_va & ~0xFFFU) | ((uint32_t)prot & 0x7U);
    return (VirtAddr)ArchInvokeSvc(SVC_MANAGEMEMORY, MNGMEM_MAP, HANDLE_ANON, (Register)size, (Register)packed);
}

static inline Err MemUnmap(VirtAddr va) {
    return ArchInvokeSvc(SVC_MANAGEMEMORY, MNGMEM_UNMAP,(Register)va, 0, 0);
}

static inline Err MemProtect(VirtAddr va, size_t size, MemProt new_prot) {
    return ArchInvokeSvc(SVC_MANAGEMEMORY, MNGMEM_PROTECT, (Register)va, (Register)size, new_prot);
}

static inline Err MemInject(Handle kitten_space_handle, VirtAddr dest_vaddr,
                             const void *src_buf, size_t size, MemProt prot, uint32_t flags) {
    InjectArgs args = {
        .size = sizeof(args),
        .dest_vaddr = dest_vaddr,
        .src_buf = src_buf,
        .len = size,
        .prot = prot,
        .flags = flags,
    };
    return ArchInvokeSvc(SVC_MANAGEMEMORY, MNGMEM_INJECT, kitten_space_handle, (Register)(VirtAddr)&args, 0);
}

static inline SvcResult Call(Handle port, uint32_t xlen, Handle grant_handle) {
    return ArchInvokeSvc4(SVC_CALL, port, (Register)xlen, grant_handle, 0);
}

static inline Err Reply(uint32_t xlen, Handle grant_handle) {
    return ArchInvokeSvc(SVC_REPLY, (Register)xlen, grant_handle, -1, 0);
}

static inline SvcResult ReplyRecv(uint32_t xlen, Handle grant_handle, Handle recv_port, Duration timeout) {
    return ArchInvokeSvc4(SVC_REPLY, (Register)xlen, grant_handle, recv_port, (Register)timeout);
}


static inline Err Signal(Handle ev_handle, EventWord bits, bool broadcast) {
    return ArchInvokeSvc(SVC_SIGNAL, ev_handle, (Register)bits,
                          broadcast ? SIGNAL_BROADCAST : 0, 0);
}

static inline Err BindMemMgmt(Handle ev_handle) {
    return ArchInvokeSvc(SVC_BIND, EVENT_MEMMGMT, ev_handle, 0, 0);
}

static inline Err BindIrq(Handle ev_handle, Handle dev_handle, uint32_t bit) {
    return ArchInvokeSvc(SVC_BIND, EVENT_IRQ, ev_handle, dev_handle, (Register)bit);
}

/** @brief type must be EVENT_PORT, EVENT_TASK, or EVENT_SPACE; target is a
 * handle of the matching kind. */
static inline Err Bind(EventType type, Handle ev_handle, Handle target, uint32_t bit) {
    return ArchInvokeSvc(SVC_BIND, type, ev_handle, target, (Register)bit);
}

/** @brief Grants handle h directly into target_space's table (you need
 * PERM_CNTL over target_space). r1 = the new handle's value in the
 * *target's* table, though the target itself has to already know the convention 
 * (e.g. "slot 0") to find it. */
static inline SvcResult HandleGrant(Handle h, Handle target_space_handle, HandlePerms perms) {
    return ArchInvokeSvc4(SVC_MANAGEHANDLE, h, MNGHNDL_GRANT, target_space_handle, perms);
}

/** @brief Unmasks the IRQ line of a device handle this space bound with
 * BindIrq. The kernel masks the line on every interrupt; call this once the
 * device's interrupt source has been cleared. */
static inline Err IrqRearm(Handle dev_handle) {
    return ArchInvokeSvc(SVC_MANAGEHANDLE, dev_handle, MNGHNDL_IRQ_REARM, 0, 0);
}

static inline int PtrIsErr(const void *p) {
    return (VirtAddr)p >= (VirtAddr)(-4095);
}

#ifdef __cplusplus
}
#endif

#endif /* _LIB_ZUZU_H */

#ifndef ZUZU_MEM_H
#define ZUZU_MEM_H

#include "zuzu/syscall_nums.h"
#include "zuzu/types.h"
#include "zuzu/memprot.h"
#include <arch/syscall.h>
#include <zuzu/spawn_args.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Memory management syscalls ---- */

/* zuzu_memmap returns a mapped VA, or a small negative errno cast to a pointer.
   The top page of the address space is the error band, so a valid VA (even one
   with the high bit set) is never misread as an error. */
static inline int ZuzuPtrIsErr(const void *p) {
    return (VirtAddr)p >= (VirtAddr)(-4095);
}

/**
 * @brief Maps a memory region into the calling process's address space.
 * 
 * @param handle The handle of the memory object to map.
 * @param size The size of the memory region to map, in bytes.
 * @param prot The desired memory protection flags (e.g., PROT_READ, PROT_WRITE).
 * 
 * @return void* Returns a pointer to the mapped virtual address, or NULL on failure.
 */
static inline void *ZuzuMemMap(Handle handle, size_t size, MemProt prot, uint32_t flags) {
    return (void *)(VirtAddr)Syscall(SYS_MEMMAP, handle, size, prot, flags);
}

/**
 * @brief Creates a shared memory region of the specified size.
 * 
 * @param size The size of the shared memory region to create, in bytes.
 * 
 * @return handle_t Returns a handle to the newly created shared memory region, or a negative value on error.
 */
static inline Handle ZuzuShmemCreate(size_t size) {
    return (Handle)Syscall(SYS_SHMEM_CREATE, size, 0, 0, 0);
}

/**
 * @brief Queries information about a device associated with the specified handle.
 * 
 * @param handle The handle of the device to query.
 * @param out_buf Pointer to the buffer where the device information will be written.
 * @param len The length of the output buffer in bytes.
 * 
 * @return int32_t Returns the IRQ number, or a negative errer code
 */
static inline Err ZuzuDeviceQuery(Handle handle, void *out_buf, size_t len) {
    return Syscall(SYS_DEV_QUERY, handle, (uint32_t)(VirtAddr)out_buf, len, 0);
}

/**
 * @brief Injects a memory region from the current process into the address space of another process.
 * @note This syscall is only callable by the init process.
 * 
 * @param taskHandle The handle of the target process.
 * @param DestVAddr The destination virtual address in the target process's address space.
 * @param src_buf Pointer to the source buffer in the current process's address space.
 * @param len The length of the source buffer in bytes.
 * @param prot The desired memory protection flags for the injected region (e.g., PROT_READ, PROT_WRITE).
 * 
 * @return Err Returns 0 on success, or a negative error code on failure.
 */
static inline Err ZuzuAsInject(Handle taskHandle, VirtAddr DestVAddr,
                                const void *src_buf, size_t len, MemProt prot) {
    AsInjectArgs args = {
        .size        = sizeof(AsInjectArgs),
        .taskHandle  = taskHandle,
        .DestVAddr   = DestVAddr,
        .src_buf     = src_buf,
        .len         = len,
        .prot        = prot,
        .flags       = 0,
    };
    return Syscall(SYS_ASINJECT, (uint32_t)(VirtAddr)&args, 0, 0, 0);
}

/**
 * @brief Reserves demand-zero anonymous memory in another task's address
 * space, without copying any bytes up front.
 * @note This syscall is only callable by the init process.
 *
 * Registers [DestVAddr, DestVAddr+len) as anonymous memory; pages are allocated
 * and zeroed lazily on first touch by the target task's own fault handler.
 * Use this instead of ZuzuAsInject() for BSS-style tails where the source
 * would just be a buffer of zeroes.
 *
 * @param taskHandle The handle of the target process.
 * @param DestVAddr The destination virtual address in the target process's address space.
 * @param len The length of the region to reserve, in bytes (must be page-aligned).
 * @param prot The desired memory protection flags for the reserved region.
 *
 * @return err Returns 0 on success, or a negative error code on failure.
 */
static inline Err ZuzuAsInjectReserve(Handle taskHandle, VirtAddr DestVAddr,
                                        size_t len, uint32_t prot) {
    AsInjectArgs args = {
        .size        = sizeof(AsInjectArgs),
        .taskHandle  = taskHandle,
        .DestVAddr   = DestVAddr,
        .src_buf     = NULL,    
        .len         = len,
        .prot        = prot,
        .flags       = ASINJECT_FLAG_RESERVE,
    };
    return Syscall(SYS_ASINJECT, (uint32_t)(VirtAddr)&args, 0, 0, 0);
}

/**
 * @brief Unmaps a memory region from the calling process's address space.
 * 
 * @param addr The starting virtual address of the memory region to unmap.
 * 
 * @return Err Returns 0 on success, or a negative error code on failure.
 */
static inline Err ZuzuMemUnmap(void *addr) {
    return Syscall(SYS_MEMUNMAP, (uint32_t)(VirtAddr)addr, 0, 0, 0);
}

/**
 * @brief Changes the memory protection of a specified memory region.
 * 
 * @param addr The starting virtual address of the memory region.
 * @param size The size of the memory region in bytes.
 * @param prot The desired memory protection flags (e.g., PROT_READ, PROT_WRITE).
 * 
 * @return Err Returns 0 on success, or a negative error code on failure.
 */
static inline Err ZuzuMemProtect(void *addr, size_t size, MemProt prot) {
    return Syscall(SYS_MEMPROTECT, (uint32_t)(VirtAddr)addr, size, prot, 0);
}

#ifdef __cplusplus
}
#endif

#endif

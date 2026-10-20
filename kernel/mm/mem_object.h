/**
 * @file mem_object.h
 * @brief Reference-counted memory objects (device windows and shared RAM) and DMA mappings.
 */
#ifndef KERNEL_MM_MEM_OBJECT_H
#define KERNEL_MM_MEM_OBJECT_H

#include "kernel/mm/pmm/pmm.h"
#include "kernel/mm/vmm/vmm.h"
#include <types.h>

/** @brief Kind of backing store a MemObject describes. */
typedef enum {
    MEMKIND_NONE,   /**< Unused / invalid object. */
    MEMKIND_DEVICE, /**< Device MMIO window at a fixed physical range. */
    MEMKIND_SHARED, /**< Shared RAM backed by a list of physical frames. */
} MemKind;

/** @brief Maximum number of live DMA mappings per device object. */
#define DMA_MAX_MAPPINGS 256

struct MemObjectStruct;

/** @brief One DMA mapping of a shared-memory range into a device's bus address space. */
typedef struct DmaMappingStruct {
    struct DmaMappingStruct *next; /**< Next mapping in the device's list. */
    uint32_t id;                   /**< Mapping id, unique within the device object. */
    struct MemObjectStruct *mem;   /**< Mapped shared-memory object (a reference is held). */
    size_t offset;                 /**< Byte offset of the mapping within @c mem. */
    size_t len;                    /**< Length of the mapping in bytes. */
    DmaDir dir;                    /**< Transfer direction of the mapping. */
    uintptr_t bus_addr;            /**< Bus address the device uses to reach the range. */
} DmaMapping;

/** @brief A reference-counted memory object, either a device window or shared RAM. */
typedef struct MemObjectStruct {
    MemKind kind;     /**< Which union member is valid. */
    size_t ref_count; /**< Number of outstanding references. */
    bool exec_synced; /**< Whether instruction/data caches have been synced for execution. */
    union {
        struct {
            PhysAddr phys_base;      /**< Physical base address of the device window. */
            size_t size;             /**< Size of the window in bytes. */
            Irq irq;                 /**< Interrupt line associated with the device. */
            uintptr_t dma_limit;     /**< Highest bus address the device can reach. */
            uintptr_t bus_offset;    /**< Offset added to a physical address to form a bus address. */
            DmaMapping *dma_maps;    /**< Head of the list of active DMA mappings. */
            uint32_t dma_next_id;    /**< Next mapping id to hand out (never 0). */
            size_t dma_map_count;    /**< Number of active DMA mappings. */
        } dev; /**< Valid when kind is MEMKIND_DEVICE. */
        struct {
            PhysAddr *page_addrs;    /**< Heap array of physical frame addresses (owned). */
            size_t page_count;       /**< Number of entries in @c page_addrs. */
            CreateMemoryFlags flags; /**< Creation flags (e.g. MEM_CONTIG, MEM_UNCACHED). */
        } shm; /**< Valid when kind is MEMKIND_SHARED. */
    };
} MemObject;

/**
 * @brief Create a shared-memory object that takes ownership of the given frames.
 * @param[in] page_addrs Heap-allocated array of physical frame addresses; ownership passes to the object.
 * @param[in] page_count Number of entries in @p page_addrs.
 * @param[in] flags Memory creation flags.
 * @return New object with reference count 1, or NULL if allocation failed.
 */
MemObject *MemObjCreateShm(PhysAddr *page_addrs, size_t page_count, CreateMemoryFlags flags);

/**
 * @brief Create a device memory object describing an MMIO window.
 * @param[in] phys_base Physical base address of the window.
 * @param[in] size Size of the window in bytes.
 * @param[in] irq Interrupt line of the device.
 * @return New object with reference count 1 and no DMA mappings, or NULL if allocation failed.
 */
MemObject *MemObjCreateDevice(PhysAddr phys_base, size_t size, Irq irq);

/**
 * @brief Take an additional reference on an object.
 * @param[in,out] mem Object to reference; NULL is ignored.
 */
void MemObjRef(MemObject *mem);

/**
 * @brief Drop a reference; frees the object when the count reaches zero.
 *
 * Freeing a shared object returns its frames to the PMM and frees the frame array.
 * Freeing a device object releases every remaining DMA mapping and its reference
 * on the mapped memory.
 *
 * @param[in,out] mem Object to release; NULL is ignored.
 */
void MemObjUnref(MemObject *mem);

/**
 * @brief Remove a mapping of an object from an address space, then drop a reference.
 * @param[in,out] as Address space the object is mapped into.
 * @param[in] mapped_va Virtual address of the mapping, or 0 if it was never mapped (skips the unmap).
 * @param[in,out] mem Object whose reference is dropped.
 */
void MemObjUnmapAndDrop(AddressSpace *as, VirtAddr mapped_va, MemObject *mem);

/**
 * @brief Map a physically contiguous shared range for DMA by a device.
 *
 * The range must be page aligned, lie within @p mem, and be reachable under the
 * device's DMA limit. For cached memory the data cache range is cleaned and
 * invalidated. The mapping holds a reference on @p mem.
 *
 * @param[in,out] dev Device object that will access the memory.
 * @param[in,out] mem Shared object created with MEM_CONTIG.
 * @param[in] offset Page-aligned byte offset into @p mem.
 * @param[in] len Page-aligned length in bytes, non-zero.
 * @param[in] dir DMA transfer direction.
 * @param[out] bus_addr Receives the bus address of the range.
 * @param[out] id Receives the mapping id.
 * @return ZUZU_OK on success, or an error code.
 * @retval ERR_BADTYPE @p dev is not a device object, or @p mem is not contiguous shared memory.
 * @retval ERR_BADARG Invalid direction, zero or unaligned range, out-of-bounds range, or beyond the DMA limit.
 * @retval ERR_NOMEM Mapping limit reached or allocation failed.
 */
Err MemObjDmaMap(MemObject *dev, MemObject *mem, size_t offset, size_t len, DmaDir dir,
                 uintptr_t *bus_addr, uint32_t *id);

/**
 * @brief Remove a DMA mapping.
 *
 * For mappings that can be written by the device, the data cache range is
 * invalidated (cached memory only) before the mapping is released.
 *
 * @param[in,out] dev Device object owning the mapping.
 * @param[in] id Mapping id returned by MemObjDmaMap().
 * @return ZUZU_OK on success, or an error code.
 * @retval ERR_BADTYPE @p dev is not a device object.
 * @retval ERR_NOENT No mapping with that id.
 */
Err MemObjDmaUnmap(MemObject *dev, uint32_t id);

/**
 * @brief Get the n-th DMA mapping of a device in list order.
 * @param[in] dev Device object.
 * @param[in] index Zero-based position in the mapping list.
 * @return The mapping, or NULL if @p dev is not a device or @p index is out of range.
 */
const DmaMapping *MemObjDmaNth(const MemObject *dev, size_t index);

/**
 * @brief Look up a DMA mapping by id.
 * @param[in] dev Device object.
 * @param[in] id Mapping id.
 * @return The mapping, or NULL if @p dev is not a device or no mapping has that id.
 */
const DmaMapping *MemObjDmaFind(const MemObject *dev, uint32_t id);

/**
 * @brief Synchronize CPU caches for a sub-range of an existing DMA mapping.
 *
 * Does nothing (and succeeds) for uncached mappings. DMA_SYNC_FOR_DEVICE cleans
 * and invalidates the range; the other operation invalidates it only if the
 * mapping can be written by the device.
 *
 * @param[in,out] dev Device object owning the mapping.
 * @param[in] bus Bus address of the start of the range; must be DMA_ALIGN aligned.
 * @param[in] len Length in bytes; non-zero and DMA_ALIGN aligned.
 * @param[in] op Sync operation (DMA_SYNC_FOR_DEVICE or DMA_SYNC_FOR_CPU).
 * @return ZUZU_OK on success, or an error code.
 * @retval ERR_BADTYPE @p dev is not a device object.
 * @retval ERR_BADARG Invalid operation, or zero/unaligned range.
 * @retval ERR_NOENT The range is not contained in any single mapping.
 */
Err MemObjDmaSync(MemObject *dev, uintptr_t bus, size_t len, DmaSyncOp op);

#endif /* KERNEL_MM_MEM_OBJECT_H */

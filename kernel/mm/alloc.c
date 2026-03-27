#include "alloc.h"
#include "pmm.h"
#include "kernel/layout.h"
#include "kernel/mm/vmm.h"  // For PA_TO_VA
#include "stdbool.h"
#include <mem.h>
#include <assert.h>
#include "core/panic.h"
#include "core/log.h"

extern kernel_layout_t kernel_layout;

kmem_block_t* heap_head = NULL;

static void heap_append_block(kmem_block_t *block)
{
    block->next = NULL;
    if (!heap_head) {
        heap_head = block;
        return;
    }

    kmem_block_t *tail = heap_head;
    while (tail->next) {
        tail = tail->next;
    }
    tail->next = block;
}

static bool kheap_grow(size_t min_payload)
{
    size_t wanted = align_up(min_payload + HDR + MIN_PAYLOAD, PAGE_SIZE);
    size_t pages = wanted / PAGE_SIZE;
    if (pages < HEAP_GROW_MIN_PAGES) {
        pages = HEAP_GROW_MIN_PAGES;
    }

    uintptr_t heap_pa = pmm_alloc_pages(pages);
    if (!heap_pa) {
        return false;
    }

    uintptr_t heap_va = PA_TO_VA(heap_pa);
    kmem_block_t *block = (kmem_block_t *)heap_va;
    block->size = align_down(pages * PAGE_SIZE - HDR, ALIGNMENT);
    block->free = true;
    block->next = NULL;

    heap_append_block(block);

    uintptr_t seg_end_pa = heap_pa + pages * PAGE_SIZE;
    uintptr_t seg_end_va = heap_va + pages * PAGE_SIZE;

    if (kernel_layout.heap_start_pa == 0 || heap_pa < kernel_layout.heap_start_pa) {
        kernel_layout.heap_start_pa = heap_pa;
        kernel_layout.heap_start_va = (void *)heap_va;
    }
    if (seg_end_pa > kernel_layout.heap_end_pa) {
        kernel_layout.heap_end_pa = seg_end_pa;
        kernel_layout.heap_end_va = (void *)seg_end_va;
    }

    return true;
}

static kmem_block_t *kheap_find_block_by_payload(void *ptr)
{
    kmem_block_t *cur = heap_head;
    while (cur) {
        if ((void *)((uint8_t *)cur + HDR) == ptr) {
            return cur;
        }
        cur = cur->next;
    }
    return NULL;
}

void* kmalloc(size_t size) {
    if (!size) {
        return NULL;
    }
    size_t req = align_up(size, ALIGNMENT); // align area up

    for (int pass = 0; pass < 2; pass++) {
        kmem_block_t* current_block = heap_head;
        while (current_block) {
            if (current_block->free && current_block->size >= req) {    // free block found?
                size_t leftover = current_block->size - req;
                if (leftover >= HDR + MIN_PAYLOAD + (ALIGNMENT - 1)) { // split?
                    kmem_block_t* new_block = (kmem_block_t*) ((uint8_t*)current_block + HDR + req);

                    new_block->size = align_down(leftover - HDR, ALIGNMENT);
                    new_block->next = current_block->next;
                    new_block->free = true;
                    current_block->next = new_block;
                    current_block->free = false;
                    current_block->size = req;
                    return (void*)((uint8_t*)current_block + HDR);
                } else {    // no split
                    current_block->free = false;
                    return (void*)((uint8_t*)current_block + HDR);
                }
            }
            current_block = current_block->next;
        }

        if (!kheap_grow(req)) {
            break;
        }
    }

    KERROR("No space in kernel heap");
    return NULL;
}


void kfree(void* ptr) {
    if (!ptr) {
        return;
    }
    
    // Sanity check: ptr must be aligned
    if (((uintptr_t)ptr % ALIGNMENT) != 0) {
        KERROR("kfree: pointer not aligned");
        return;
    }
    
    kmem_block_t* header = kheap_find_block_by_payload(ptr);
    if (!header) {
        KERROR("kfree: pointer does not match any allocated heap block");
        return;
    }
    
    if (header->free) {
        KERROR("Double free in kernel heap");
        return;
    }
    header->free = true;
    
    // Forward merge: merge freed block with its next repeatedly
    while (header->next) {
        if (header->next->free) {
            uint8_t* block_end = (uint8_t*)header + HDR + header->size;
            if (block_end == (uint8_t*)header->next) {
                header->size += HDR + header->next->size;
                header->next = header->next->next;
            } else {
                break; // Not adjacent, stop merging
            }
        } else {
            break; // Next not free, stop merging
        }
    }
    
    // Backward merge: find previous block
    kmem_block_t* prev = NULL;
    if (header != heap_head) {
        prev = heap_head;
        while (prev && prev->next != header) {
            prev = prev->next;
        }
    }
    
    // Only attempt backward merge if we found a valid prev
    if (prev && prev->free) {
        uint8_t* end_of_prev = (uint8_t*)prev + HDR + prev->size;
        if (end_of_prev == (uint8_t*)header) {
            // Merge prev with header
            prev->size += HDR + header->size;
            prev->next = header->next;
            
            // Forward merge again from prev (it might now be adjacent to a free block)
            while (prev->next) {
                if (prev->next->free) {
                    uint8_t* block_end = (uint8_t*)prev + HDR + prev->size;
                    if (block_end == (uint8_t*)prev->next) {
                        prev->size += HDR + prev->next->size;
                        prev->next = prev->next->next;
                    } else {
                        break; // Not adjacent, stop merging
                    }
                } else {
                    break; // Next not free, stop merging
                }
            }
        }
    }
}


void kheap_init(void) {
    kassert(HEAP_INITIAL_SIZE % PAGE_SIZE == 0);

    heap_head = NULL;
    kernel_layout.heap_start_pa = 0;
    kernel_layout.heap_end_pa = 0;
    kernel_layout.heap_start_va = NULL;
    kernel_layout.heap_end_va = NULL;

    if (!kheap_grow(HEAP_INITIAL_SIZE - HDR)) {
        panic("Heap could not be allocated");
    }
}


void kheap_dump(void) {
    KINFO("*** HEAP DUMP ***");
    // Print both PA and VA for clarity
    KINFO("Heap PA: 0x%X - 0x%X", kernel_layout.heap_start_pa, kernel_layout.heap_end_pa);
    KINFO("Heap VA: %p - %p", kernel_layout.heap_start_va, kernel_layout.heap_end_va);
    
    kmem_block_t* current = heap_head;
    int block_num = 0;
    size_t total_free = 0;
    size_t total_used = 0;
    
    while (current) {
        // Sanity check: ensure current is within heap bounds (use _va)
        if ((uint8_t*)current < (uint8_t*)kernel_layout.heap_start_va ||
            (uint8_t*)current >= (uint8_t*)kernel_layout.heap_end_va) {
            KERROR("Block %d corrupted - pointer %p outside heap bounds", block_num, current);
            break;
        }
        
        KINFO("Block %d: addr=%p size=%u free=%d next=%p", 
              block_num, current, current->size, current->free, current->next);
        
        if (current->free) {
            total_free += current->size;
        } else {
            total_used += current->size;
        }
        
        current = current->next;
        block_num++;
        
        // Prevent infinite loop in case of corruption
        if (block_num > 1000) {
            KERROR("Too many blocks - possible corruption");
            break;
        }
    }
    
    KINFO("Total blocks: %d, Free: %u bytes, Used: %u bytes", 
          block_num, total_free, total_used);
}
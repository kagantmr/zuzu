#include "alloc.h"
#include "pmm.h"
#include "kernel/layout.h"
#include "kernel/vmm/vmm.h"  // For PA_TO_VA
#include "stdbool.h"
#include "lib/mem.h"
#include "core/assert.h"

extern kernel_layout_t kernel_layout;

kmem_block_t* heap_head = NULL;

void* kmalloc(size_t size) {
    if (!size) {
        return NULL;
    }
    size_t req = align_up(size, ALIGNMENT); // align area up
    kmem_block_t* current_block = heap_head;
    while (current_block) {
        if (current_block->free && current_block->size >= req) {    // free block found?
            size_t leftover = current_block->size - req;
            if (leftover >= HDR + MIN_PAYLOAD + (ALIGNMENT - 1)) { // split?
                kmem_block_t* new_block = (kmem_block_t*) ((uint8_t*)current_block + HDR + req);
                
                // Sanity check: ensure new_block header fits within heap bounds
                // Use _va since we're comparing pointers (VAs)
                if ((uint8_t*)new_block + HDR > (uint8_t*)kernel_layout.heap_end_va) {
                    KERROR("kmalloc: split would create block outside heap bounds");
                    return NULL;
                }
                
                new_block->size = align_down(leftover - HDR, ALIGNMENT);
                new_block->next = current_block->next;
                new_block->free = true;
                current_block->next = new_block;
                current_block->free = false;
                current_block->size = req;
                return (void*)((uint8_t*)current_block + HDR);
            } else {    // no split
                current_block->free = false;
                current_block->size = req; // Set to requested size for consistent reporting
                return (void*)((uint8_t*)current_block + HDR);
            }
        }
        current_block = current_block->next; 
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
    
    // Sanity check: ptr must be within heap bounds (use _va for pointer comparisons)
    if ((uint8_t*)ptr < (uint8_t*)kernel_layout.heap_start_va || 
        (uint8_t*)ptr >= (uint8_t*)kernel_layout.heap_end_va) {
        KERROR("kfree: pointer outside heap bounds");
        return;
    }
    
    kmem_block_t* header  = (kmem_block_t*)((uint8_t*)ptr - HDR);
    
    // Sanity check: header must also be within heap bounds
    if ((uint8_t*)header < (uint8_t*)kernel_layout.heap_start_va || 
        (uint8_t*)header >= (uint8_t*)kernel_layout.heap_end_va) {
        KERROR("kfree: computed header outside heap bounds");
        return;
    }
    
    if (header->free) {
        KERROR("Double free in kernel heap");
        return;
    }
    header->free = true;
    
    // Forward merge: merge freed block with its next repeatedly
    while (header->next) {
        // Sanity check: ensure next is within heap bounds
        if ((uint8_t*)header->next < (uint8_t*)kernel_layout.heap_start_va ||
            (uint8_t*)header->next >= (uint8_t*)kernel_layout.heap_end_va) {
            KERROR("kfree: corrupted next pointer in forward merge");
            break;
        }
        
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
                // Sanity check: ensure next is within heap bounds
                if ((uint8_t*)prev->next < (uint8_t*)kernel_layout.heap_start_va ||
                    (uint8_t*)prev->next >= (uint8_t*)kernel_layout.heap_end_va) {
                    KERROR("kfree: corrupted next pointer in backward merge forward pass");
                    break;
                }
                
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
    // Request 1 MB of heap (256 pages at 4KB/page)
    kassert(HEAP_SIZE % PAGE_SIZE == 0);    // Check alignment
    
    // PMM returns physical addresses
    uintptr_t heap_pa = pmm_alloc_pages(HEAP_SIZE/PAGE_SIZE);
    if (!heap_pa) {
        KPANIC("Heap could not be allocated");
    }
    
    // Store PHYSICAL addresses in _pa fields (for debug/bookkeeping)
    kernel_layout.heap_start_pa = heap_pa;
    kernel_layout.heap_end_pa   = heap_pa + HEAP_SIZE;
    
    // Store VIRTUAL addresses in _va fields (for actual pointer use)
    kernel_layout.heap_start_va = (void*)PA_TO_VA(heap_pa);
    kernel_layout.heap_end_va   = (void*)PA_TO_VA(heap_pa + HEAP_SIZE);

    // Use VA for heap_head - this is a dereferenceable pointer!
    heap_head = (kmem_block_t*) kernel_layout.heap_start_va;
    heap_head->size = align_down(HEAP_SIZE - HDR, ALIGNMENT);
    heap_head->next = NULL;
    heap_head->free = true;
}


void kheap_dump(void) {
    KINFO("*** HEAP DUMP ***");
    // Print both PA and VA for clarity
    KINFO("Heap PA: 0x%x - 0x%x", kernel_layout.heap_start_pa, kernel_layout.heap_end_pa);
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
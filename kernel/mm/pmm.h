#ifndef PMM_H
#define PMM_H

#include <stdint.h>
#include <stddef.h>

#define PAGE_SIZE 4096 // A page is 4KB

#define MARK_OK 0
#define MARK_FAIL -1
#define UNMARK_OK 0
#define UNMARK_FAIL -1
#define DOUBLE_FREE -1
#define FREE_OK 0
#define FREE_FAIL 1

typedef struct {
    uintptr_t pfn_base;    // lowest page frame number
    uintptr_t pfn_end;     // highest page frame number (exclusive)
    size_t    total_pages; // total number of pages
    size_t    free_pages;  // updated at runtime

    uint8_t*  bitmap;      // pointer to bitmap memory
    size_t    bitmap_bytes;// size of bitmap in bytes
} pmm_state_t;


/**
 * @brief Initialize the physical memory manager.
 * This sets up the bitmap and marks reserved regions.
 */
void pmm_init(void);

/**
 * @brief Mark a range of physical pages as used. 
 * @return MARK_OK if successful, MARK_FAIL if the addresses are invalid.
 */
int pmm_mark_range(uintptr_t start, uintptr_t end);

/**
 * @brief Unmark a range of pages.
 * @return UNMARK_OK if successful, UNMARK_FAIL if the addresses are invalid.
 */
int pmm_unmark_range(uintptr_t start, uintptr_t end);

/**
 * @brief Allocates a physical page, and returns a pointer to it.
 * @return Address of the allocated page.
 */
uintptr_t pmm_alloc_page(void);

/**
 * @brief Allocates contiguous physical pages.
 * @param n_pages Number of pages to allocate.
 * @return Address of the first allocated page.
 */
uintptr_t pmm_alloc_pages(size_t n_pages);

/**
 * @brief Marks an allocated page as unallocated.
 * @param addr Address of the allocated page.
 */
int pmm_free_page(uintptr_t addr);

/**
 * @brief Allocates contiguous physical pages with specific alignment.
 * @param n_pages Number of pages to allocate.
 * @param align_pages Alignment in pages (must be power of two).
 * @return Address of the first allocated page.
 */
uintptr_t pmm_alloc_pages_aligned(size_t n_pages, size_t align_pages);

#endif
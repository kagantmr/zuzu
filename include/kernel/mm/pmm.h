#ifndef PMM_H
#define PMM_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    uintptr_t pfn_base;    // lowest page frame number
    uintptr_t pfn_end;     // highest page frame number (exclusive)
    size_t    total_pages; // total number of pages
    size_t    free_pages;  // updated at runtime

    uint8_t*  bitmap;      // pointer to bitmap memory
    size_t    bitmap_bytes;// size of bitmap in bytes
} pmm_state_t;

#endif
/**
 * @file
 * @brief Program break over one demand-paged reservation.
 */

#include <sbrk.h>
#include <zuzu/zuzu.h>

/** @brief Size of the heap reservation; demand-paged, so it costs no RAM until touched. */
#define HEAP_RESERVE (32 * 1024 * 1024)

/** @brief This process's heap. */
static Arena heap;

void *sbrk(intptr_t incr)
{
    if (!heap.base) {
        VirtAddr p = (VirtAddr)MemMapAnon(HEAP_RESERVE, 0, PROT_READ | PROT_WRITE);
        if (PtrIsErr((void *)p))
            return (void *)-1;
        heap.base = p;
        heap.brk = p;
        heap.mapped = p + HEAP_RESERVE;
    }

    VirtAddr old = heap.brk;

    if (incr > 0 && (size_t)incr > heap.mapped - heap.brk)
        return (void *)-1; /* out of reservation */
    if (incr < 0 && (size_t)(-incr) > heap.brk - heap.base)
        return (void *)-1; /* would go below base */

    heap.brk += incr;
    return (void *)old;
}
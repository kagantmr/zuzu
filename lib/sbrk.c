#include <sbrk.h>
#include <zuzu/umem.h>
#include <zuzu/memprot.h>

#define HEAP_RESERVE (32 * 1024 * 1024)   /* VA reservation; demand-paged, costs no RAM until touched */

static arena_t heap;   /* the single owner of heap VA in this process */

void *sbrk(intptr_t incr)
{
    if (!heap.base) {
        VirtAddr p = (VirtAddr)ZuzuMemMap(HANDLE_ANON, HEAP_RESERVE,
                                     PROT_READ | PROT_WRITE, 0);
        if (ZuzuPtrIsErr((void *)p))
            return (void *)-1;
        heap.base   = p;
        heap.brk    = p;
        heap.mapped = p + HEAP_RESERVE;
    }

    VirtAddr old = heap.brk;

    if (incr > 0 && (size_t)incr > heap.mapped - heap.brk)
        return (void *)-1;                       /* out of reservation */
    if (incr < 0 && (size_t)(-incr) > heap.brk - heap.base)
        return (void *)-1;                       /* would go below base */

    heap.brk += incr;
    return (void *)old;
}
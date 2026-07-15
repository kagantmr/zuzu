#include <sbrk.h>
#include <zuzu/umem.h>
#include <zuzu/memprot.h>

#define HEAP_RESERVE (32 * 1024 * 1024)   /* VA reservation; demand-paged, costs no RAM until touched */

static arena_t heap;   /* the single owner of heap VA in this process */

void *sbrk(intptr_t incr)
{
    if (!heap.base) {
        vaddr_t p = (vaddr_t)zuzu_memmap(HANDLE_ANON, HEAP_RESERVE,
                                     VM_PROT_READ | VM_PROT_WRITE, 0);
        if (zuzu_is_err((void *)p))
            return (void *)-1;
        heap.base   = p;
        heap.brk    = p;
        heap.mapped = p + HEAP_RESERVE;
    }

    vaddr_t old = heap.brk;

    if (incr > 0 && (size_t)incr > heap.mapped - heap.brk)
        return (void *)-1;                       /* out of reservation */
    if (incr < 0 && (size_t)(-incr) > heap.brk - heap.base)
        return (void *)-1;                       /* would go below base */

    heap.brk += incr;
    return (void *)old;
}
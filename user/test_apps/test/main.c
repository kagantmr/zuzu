#include "zuzu/memprot.h"
#include "zuzu/types.h"
#include <stdio.h>
#include <string.h>
#include <zuzu/syspage.h>
#include <zuzu/event.h>
#include <zuzu/ntfn.h>  
#include <zuzu/umem.h>     
#include <stdbool.h>

#define MAX_ITERS 100

int main(void) {
    // 1. Setup

	Syspage *syspage = SYSPAGE;

    Handle ntfn = ZuzuNtfnCreate();
    ZuzuKEventBind(KEVENT_MEMMGMT, ntfn, NULL);
    
    printf("start: %u pages free\n", syspage->mem_free_kb);

	bool first_fired = false, second_fired = false;

    // 2. Allocate chunks until KEvent fires
    const size_t CHUNK_PAGES = 64;   // 256KB per MemMap
	int iterations = 0;
	void *chunks[MAX_ITERS];
    for (iterations = 0; iterations < MAX_ITERS; iterations++) {
        chunks[iterations] = ZuzuMemMap(HANDLE_ANON, CHUNK_PAGES * 4096, PROT_RW, 0);  // fill in args
        if (!chunks[iterations]) {
            printf("MemMap failed at iter %d — OOM before KEvent?\n", iterations);
            break;
        }
		memset(chunks[iterations], 0, CHUNK_PAGES * 4096);   // force fault-in

        NtfnBits bits = ZuzuNtfnWait(ntfn, TIMEOUT_POLL);
        if (bits & KEVENT_MEMMGMT_BIT) {
            printf("FIRED at iter %d, free=%u pages\n",
                   iterations, syspage->mem_free_kb / 4);
			first_fired = true;
            break;
        }
        
        if (iterations % 10 == 0)
            printf("iter %d: free=%u\n", iterations, syspage->mem_free_kb / 4);
    }
	// 3. Re-arm test
	for (int i = 0; i < iterations; i++) {
		ZuzuMemUnmap(chunks[i]);   // you'll need an array to track them
	}
	// free is now back near starting level, in_pressure still true

	// Drain any stale bits from the first fire
	ZuzuNtfnWait(ntfn, TIMEOUT_POLL);

    for (iterations = 0; iterations < MAX_ITERS; iterations++) {
        chunks[iterations] = ZuzuMemMap(HANDLE_ANON, CHUNK_PAGES * 4096, PROT_RW, 0);  // fill in args
        if (!chunks[iterations]) {
            printf("MemMap failed at iter %d — OOM before KEvent?\n", iterations);
            break;
        }
		memset(chunks[iterations], 0, CHUNK_PAGES * 4096);   // force fault-in

        NtfnBits bits = ZuzuNtfnWait(ntfn, TIMEOUT_POLL);
        if (bits & KEVENT_MEMMGMT_BIT) {
			second_fired = true;
            printf("FIRED at iter %d, free=%u pages\n",
                   iterations, syspage->mem_free_kb / 4);
            break;
        }
        
        if (iterations % 10 == 0)
            printf("iter %d: free=%u\n", iterations, syspage->mem_free_kb / 4);
    }

	if (first_fired && second_fired)
		printf("TEST PASSED\n");
	else
		printf("TEST FAILED: first=%d second=%d\n", first_fired, second_fired);

    return 0;
}
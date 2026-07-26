#include <zuzu/types.h>

static inline uint32_t read_pmccntr(void) {
    uint32_t cycles;
    __asm__ volatile("mrc p15, 0, %0, c9, c13, 0" : "=r"(cycles));
    return cycles;
}

/* Memory barrier to enforce strict execution order around the measured region. */
static inline void barrier(void) {
    __asm__ volatile("isb" : : : "memory");
}
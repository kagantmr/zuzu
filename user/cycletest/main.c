#include <zuzu/types.h>
#include <zuzu/perftools.h>
#include <zuzu/task.h>
#include <zuzu/ipc.h>
#include <stdio.h>

int main()
{
    uint64_t avg_cycles = 0;
    for (int i = 0; i < 1000; i++) {
        uint32_t start = read_cycle_counter();
        (void)_getpid();
        uint32_t end = read_cycle_counter();
        avg_cycles += (end - start);
    }

    printf("getpid(): %llu cycles\n", avg_cycles / 1000);

    avg_cycles = 0;
    for (int i = 0; i < 1000; i++) {
        uint32_t start = read_cycle_counter();
        (void)_ep_create();
        uint32_t end = read_cycle_counter();
        avg_cycles += (end - start);
    }

    printf("port_create(): %llu cycles\n", avg_cycles / 1000);
    return 0;
}
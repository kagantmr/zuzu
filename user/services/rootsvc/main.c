#include <arch/syscall.h>
#include <syscall_nums.h>

static const char msg[] = "rootsvc: hello\n";

int main(void)
{
    ArchInvokeSvc(SVC_LOG, (uint32_t)(uintptr_t)msg, sizeof(msg) - 1, 0, 0);
    return 0;
}
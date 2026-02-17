#include "process.h"
#include "kernel/mm/alloc.h"
#include "kernel/mm/pmm.h"
#include "arch/arm/mmu/mmu.h"
#include "kernel/sched/sched.h"
#include "kstack.h"

static uint32_t next_pid = 1;

process_t *process_create(void (*entry)(void), const uint32_t magic)
{
    (void)entry;
    process_t *process = kmalloc(sizeof(process_t));
    process->as = addrspace_create(ADDRSPACE_USER);
    uintptr_t stack_top = kstack_alloc();
    process->kernel_stack_top = stack_top;

    // write exception frame to the stack
    stack_top -= 16 * sizeof(uint32_t); // 16 words
    uint32_t *exc_frame = (uint32_t *)stack_top;
    *(exc_frame++) = 0;
    for (int i = 0; i < 12; i++)
    {
        *(exc_frame++) = 0; // r1-12 = 0  (indices 0-12)
    }
    *(exc_frame++) = USR_SP;  // lr = SP_usr              (index 13)
    *(exc_frame++) = 0x10000; // PC = entry point    (index 14)
    *(exc_frame++) = 0x10;    // CPSR = 0x10         (index 15)

    // write cpu_context to stack
    stack_top -= sizeof(cpu_context_t);
    cpu_context_t *context = (cpu_context_t *)stack_top;
    context->r4 = 0;
    context->r5 = 0;
    context->r6 = 0;
    context->r7 = 0;
    context->r8 = 0;
    context->r9 = 0;
    context->r10 = 0;
    context->r11 = 0;
    context->lr = (uint32_t)process_entry_trampoline;

    process->kernel_sp = (uint32_t *)stack_top;
    process->process_state = PROCESS_READY;
    process->pid = next_pid++;
    process->parent_pid = 0; // No parent for now

    process->priority = 1;   // Default priority
    process->time_slice = 5; // Default time slice
    process->ticks_remaining = process->time_slice;
    process->node.next = NULL;
    process->node.prev = NULL;

    uintptr_t program_page_pa = pmm_alloc_page();
    *(volatile uint32_t *)(PA_TO_VA(program_page_pa)) = magic;

    kmap_user_page(process->as, program_page_pa, 0x10000, VM_PROT_READ | VM_PROT_WRITE);

    uintptr_t user_stack_pa = pmm_alloc_pages(4);
    for (int i = 0; i < 4; i++)
    {
        kmap_user_page(process->as, user_stack_pa + i * 0x1000,
                       0x7FFFC000 + i * 0x1000, VM_PROT_READ | VM_PROT_WRITE);
    }

    // EXPERIMENTAL PROCESS TEST!!!!!!
    // TODO: Clean up after ELF loading is activated
    uint32_t *code = (uint32_t *)(PA_TO_VA(program_page_pa));
    switch (magic)
    {
    case 0xDEADBEEF: // "The Yielder" - Stresses the scheduler
        // Loops 100,000 times calling SYS_TASK_YIELD
        code[0] = 0xE30846A0; // MOVW R4, #0x86A0 (lower 16 bits of 100,000)
        code[1] = 0xE3404001; // MOVT R4, #0x0001 (upper 16 bits) -> R4 = 100,000
        // loop:
        code[2] = 0xEF000001; // SVC #0x01 (SYS_TASK_YIELD)
        code[3] = 0xE2544001; // SUBS R4, R4, #1  (Decrement counter, set flags)
        code[4] = 0x1AFFFFFC; // BNE -4 words (Jump back to SVC if R4 != 0)
        // exit:
        code[5] = 0xE3A00000; // MOV R0, #0 (Success)
        code[6] = 0xEF000000; // SVC #0x00 (SYS_TASK_QUIT)
        code[7] = 0xEAFFFFFE; // B . (Spin safety)
        break;

    case 0xBAD0B010: // "The Trespasser" - Memory Protection Test
        // Attempts to write to Kernel Memory (0xC0000000)
        // Should trigger a DATA ABORT in the kernel.
        code[0] = 0xE3A00000; // MOV R0, #0
        code[1] = 0xE34C0000; // MOVT R0, #0xC000  (R0 = 0xC0000000)
        code[2] = 0xE5800000; // STR R0, [R0]      (Write to 0xC0000000)
        code[3] = 0xEF000000; // SVC #0x00 (Quit if it somehow survives)
        code[4] = 0xEAFFFFFE; // B .
        break;

    case 0x11111111: // "The Listener" - IPC Receiver
        // 1. Create a Port (SYS_PORT_CREATE = 0x20)
        code[0] = 0xEF000020; // SVC #0x20
        code[1] = 0xE1A04000; // MOV R4, R0 (Save port handle)

        // loop: (This is the target)
        // 2. Log "Wait...\n"
        code[2] = 0xE28F0020; // ADD R0, PC, #32 (Points to code[12])
        code[3] = 0xE3A01008; // MOV R1, #8
        code[4] = 0xEF0000F0; // SVC #0xF0 (SYS_LOG)

        // 3. Receive (SYS_PROC_RECV = 0x11)
        code[5] = 0xE1A00004; // MOV R0, R4
        code[6] = 0xEF000011; // SVC #0x11 (Blocks)

        // 4. Log "Got it!\n"
        code[7] = 0xE28F0014; // ADD R0, PC, #20 (Points to code[14])
        code[8] = 0xE3A01008; // MOV R1, #8
        code[9] = 0xEF0000F0; // SVC #0xF0 (SYS_LOG)

        // 5. Branch back to code[2]
        // To jump from code[10] to code[2]:
        // PC at code[10] is code[12].
        // We need to go back 10 words (40 bytes).
        // The immediate in the B instruction is (offset - 8) >> 2.
        // (-40 - 8) >> 2 = -48 >> 2 = -12.
        // Two's complement for -12 in 24 bits is 0xFFFFF4.
        code[10] = 0xEAFFFFF4; // B code[2] (Re-calculated)
        code[11] = 0xE1A00000; // NOP

        // --- DATA SECTION ---
        code[12] = 0x74696157; // "Wait"
        code[13] = 0x0A2E2E2E; // "...\n"
        code[14] = 0x20746F47; // "Got "
        code[15] = 0x0A217469; // "it!\n"
        break;
    case 0x22222222: // "The Messenger" - IPC Sender
        // 1. Sleep (SYS_TASK_SLEEP = 0x05)
        code[0] = 0xE3A00FA0; // MOV R0, #1000
        code[1] = 0xEF000005; // SVC #0x05

        // 2. Prepare Payload (r1-r3)
        code[2] = 0xE3A0100A; // MOV R1, #10
        code[3] = 0xE3A02014; // MOV R2, #20
        code[4] = 0xE3A0301E; // MOV R3, #30

        // 3. Send (SYS_PROC_SEND = 0x10)
        code[5] = 0xE3A00000; // MOV R0, #0 (Port 0)
        code[6] = 0xEF000010; // SVC #0x10

        // 4. Log "Sent!\n"
        code[7] = 0xE28F0008; // ADD R0, PC, #8 (Pointer to "Sent!\n")
        code[8] = 0xE3A01006; // MOV R1, #6
        code[9] = 0xEF0000F0; // SVC #0xF0 (SYS_LOG)

        // 5. Quit (SYS_TASK_QUIT = 0x00)
        code[10] = 0xEF000000;

        // Data
        code[11] = 0x746E6553; // "Sent"
        code[12] = 0x00000A21; // "!\n\0\0"
        break;
    case 0xABABABAB: // "The Napper" - Precision Fixed
        // 1. Log "Yawn...\n"
        // PC is at code[0]+8 = code[2].
        // Data is at code[11]. (11 - 2) * 4 bytes = 36 bytes (0x24)
        code[0] = 0xE28F0024; // ADD R0, PC, #36
        code[1] = 0xE3A01008; // MOV R1, #8 (Y, a, w, n, ., ., ., \n)
        code[2] = 0xEF0000F0; // SYS_LOG

        // 2. Sleep 1000ms
        code[3] = 0xE3A00FA0;
        code[4] = 0xEF000005; // SYS_TASK_SLEEP (0x05)

        // 3. Log "Awake!\n"
        // PC is at code[5]+8 = code[7].
        // Data is at code[13]. (13 - 7) * 4 bytes = 24 bytes (0x18)
        code[5] = 0xE28F0018; // ADD R0, PC, #24
        code[6] = 0xE3A01007; // MOV R1, #7 (A, w, a, k, e, !, \n)
        code[7] = 0xEF0000F0; // SYS_LOG

        code[8] = 0xEF000000;  // SYS_TASK_QUIT
        code[9] = 0xEAFFFFFE;  // Spin safety
        code[10] = 0xE1A00000; // NOP (Ensures code[11] is 4-byte aligned)

        // --- DATA SECTION ---
        // Little Endian packing:
        code[11] = 0x6E776159; // "Yawn"
        code[12] = 0x0A2E2E2E; // "...\n"

        code[13] = 0x6B617741; // "Awak"
        code[14] = 0x000A2165; // "e!\n\0"
        break;
    default: // "The Spinner" - CPU Burner
        // Just spins forever. Good for testing preemption.
        code[0] = 0xE3A00000; // MOV R0, #0
        code[1] = 0xEAFFFFFE; // B . (Infinite Loop)
        break;
    }

    // user VA for code should start at first page, stack could be ...idk? N=1 means we get a 2gb/2gb split
    // allocate those
    // copy code, memcpy() probably
    // copy stack, memcpy() prob.
    // set entry point

    return process;
}

void process_destroy(process_t *p)
{
    if (p->as)
    {
        arch_mmu_free_user_pages(p->as->ttbr0_pa);
        arch_mmu_free_tables(p->as->ttbr0_pa, p->as->type);
        if (p->as->regions)
            kfree(p->as->regions);
        kfree(p->as);
    }
    kstack_free(p->kernel_stack_top);
    kfree(p);
    // sched_defer_destroy(p);
}

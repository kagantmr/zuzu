#include "process.h"
#include "kernel/mm/alloc.h"
#include "kernel/mm/pmm.h"
#include "arch/arm/mmu/mmu.h"
#include "kernel/sched/sched.h"
#include "lib/mem.h"
#include "core/log.h"
#include "kstack.h"

static uint32_t next_pid = 1;
process_t *process_table[MAX_PROCESSES];

process_t *process_find_by_pid(uint32_t pid)
{
    if (pid >= MAX_PROCESSES)
        return NULL;
    return process_table[pid];
}

process_t *process_create(void (*entry)(void), const uint32_t magic)
{
    (void)entry;
    process_t *process = kmalloc(sizeof(process_t));
    memset(process, 0, sizeof(process_t));
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
    process_table[process->pid] = process;
    process->parent_pid = 0;

    process->priority = 1;
    process->time_slice = 5;
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

    uint32_t *code = (uint32_t *)(PA_TO_VA(program_page_pa));
    switch (magic)
    {
    case 0xDEADBEEF: // "The Yielder"
        code[0] = 0xE30846A0; // MOVW R4, #0x86A0
        code[1] = 0xE3404001; // MOVT R4, #0x0001
        code[2] = 0xEF000001; // SVC #0x01 (yield)
        code[3] = 0xE2544001; // SUBS R4, R4, #1
        code[4] = 0x1AFFFFFC; // BNE code[2]
        code[5] = 0xE3A00000; // MOV R0, #0
        code[6] = 0xEF000000; // SVC #0x00 (exit)
        code[7] = 0xEAFFFFFE; // B .
        break;

    case 0xBAD0B010: // "The Trespasser"
        code[0] = 0xE3A00000; // MOV R0, #0
        code[1] = 0xE34C0000; // MOVT R0, #0xC000
        code[2] = 0xE5800000; // STR R0, [R0]
        code[3] = 0xEF000000; // SVC #0x00
        code[4] = 0xEAFFFFFE; // B .
        break;

    case 0x11111111: // "The Listener" (handle 0 pre-assigned)
        code[0] = 0xE28F001C; // ADD R0, PC, #28 -> code[9]
        code[1] = 0xE3A01008; // MOV R1, #8
        code[2] = 0xEF0000F0; // SVC #0xF0
        code[3] = 0xE3A00000; // MOV R0, #0
        code[4] = 0xEF000011; // SVC #0x11 (recv)
        code[5] = 0xE28F0010; // ADD R0, PC, #16 -> code[11]
        code[6] = 0xE3A01007; // MOV R1, #7
        code[7] = 0xEF0000F0; // SVC #0xF0
        code[8] = 0xEAFFFFF6; // B code[0]
        code[9]  = 0x74696157; // "Wait"
        code[10] = 0x0A2E2E2E; // "...\n"
        code[11] = 0x20746F47; // "Got "
        code[12] = 0x0A217469; // "it!\n"
        break;

    case 0x22222222: // "The Messenger"
        code[0] = 0xE3A00FA0; // MOV R0, #1000
        code[1] = 0xEF000005; // SVC #0x05 (sleep)
        code[2] = 0xE3A0100A; // MOV R1, #10
        code[3] = 0xE3A02014; // MOV R2, #20
        code[4] = 0xE3A0301E; // MOV R3, #30
        code[5] = 0xE3A00000; // MOV R0, #0
        code[6] = 0xEF000010; // SVC #0x10 (send)
        code[7] = 0xE28F0008; // ADD R0, PC, #8 -> code[11]
        code[8] = 0xE3A01006; // MOV R1, #6
        code[9] = 0xEF0000F0; // SVC #0xF0
        code[10] = 0xEF000000; // SVC #0x00 (exit)
        code[11] = 0x746E6553; // "Sent"
        code[12] = 0x00000A21; // "!\n\0\0"
        break;

    case 0xABABABAB: // "The Napper"
        code[0] = 0xE28F0024; // ADD R0, PC, #36 -> code[11]
        code[1] = 0xE3A01008; // MOV R1, #8
        code[2] = 0xEF0000F0; // SYS_LOG
        code[3] = 0xE3A00FA0; // MOV R0, #1000
        code[4] = 0xEF000005; // SYS_TASK_SLEEP
        code[5] = 0xE28F0018; // ADD R0, PC, #24 -> code[13]
        code[6] = 0xE3A01007; // MOV R1, #7
        code[7] = 0xEF0000F0; // SYS_LOG
        code[8] = 0xEF000000;  // SYS_TASK_QUIT
        code[9] = 0xEAFFFFFE;  // B .
        code[10] = 0xE1A00000; // NOP
        code[11] = 0x6E776159; // "Yawn"
        code[12] = 0x0A2E2E2E; // "...\n"
        code[13] = 0x6B617741; // "Awak"
        code[14] = 0x000A2165; // "e!\n\0"
        break;

    /* ================================================================
     * IPC TEST A: Ping-Pong
     *   Pinger: send 0xDEADBEEF → recv reply → log → exit(reply)
     *   Ponger: recv → send 0xCAFEBABE → log → exit(received)
     * ================================================================ */
    case 0xAAAA0001: // "Pinger"
        code[0]  = 0xE30B1EEF; // MOVW R1, #0xBEEF
        code[1]  = 0xE34D1EAD; // MOVT R1, #0xDEAD
        code[2]  = 0xE3A00000; // MOV R0, #0
        code[3]  = 0xEF000010; // SVC #0x10 (send)
        code[4]  = 0xE3A00000; // MOV R0, #0
        code[5]  = 0xEF000011; // SVC #0x11 (recv)
        code[6]  = 0xE1A04001; // MOV R4, R1 (save reply)
        // Log "Ping:OK\n"
        code[7]  = 0xE28F0010; // ADD R0, PC, #16 (PC=code[9], +16=code[13])
        code[8]  = 0xE3A01008; // MOV R1, #8
        code[9]  = 0xEF0000F0; // SVC #0xF0
        code[10] = 0xE1A00004; // MOV R0, R4
        code[11] = 0xEF000000; // SVC #0x00 (exit)
        code[12] = 0xEAFFFFFE; // B .
        // DATA at code[13]
        code[13] = 0x676E6950; // "Ping"
        code[14] = 0x0A4B4F3A; // ":OK\n"
        break;

    case 0xAAAA0002: // "Ponger"
        code[0]  = 0xE3A00000; // MOV R0, #0
        code[1]  = 0xEF000011; // SVC #0x11 (recv)
        code[2]  = 0xE1A04000; // MOV R4, R0 (sender PID)
        code[3]  = 0xE1A05001; // MOV R5, R1 (received payload)
        // Send 0xCAFEBABE back
        code[4]  = 0xE30B1ABE; // MOVW R1, #0xBABE
        code[5]  = 0xE34C1AFE; // MOVT R1, #0xCAFE
        code[6]  = 0xE3A00000; // MOV R0, #0
        code[7]  = 0xEF000010; // SVC #0x10 (send)
        // Log "Pong:OK\n"
        code[8]  = 0xE28F0010; // ADD R0, PC, #16 (PC=code[10], +16=code[14])
        code[9]  = 0xE3A01008; // MOV R1, #8
        code[10] = 0xEF0000F0; // SVC #0xF0
        // Exit with received payload (expect 0xDEADBEEF)
        code[11] = 0xE1A00005; // MOV R0, R5
        code[12] = 0xEF000000; // SVC #0x00
        code[13] = 0xEAFFFFFE; // B .
        // DATA at code[14]
        code[14] = 0x676E6F50; // "Pong"
        code[15] = 0x0A4B4F3A; // ":OK\n"
        break;

    /* ================================================================
     * IPC TEST B: Sender blocks first
     *   Sender sends immediately (blocks waiting for receiver)
     *   Receiver sleeps 500ms then recvs
     * ================================================================ */
    case 0xBBBB0001: // "Sender-first"
        code[0]  = 0xE3A01042; // MOV R1, #0x42
        code[1]  = 0xE3A00000; // MOV R0, #0
        code[2]  = 0xEF000010; // SVC #0x10 (send — blocks)
        // Log "SndOK!\n"
        code[3]  = 0xE28F0008; // ADD R0, PC, #8 (PC=code[5], +8=code[7])
        code[4]  = 0xE3A01007; // MOV R1, #7
        code[5]  = 0xEF0000F0; // SVC #0xF0
        code[6]  = 0xEF000000; // SVC #0x00 (exit)
        // DATA at code[7]
        code[7]  = 0x4F646E53; // "SndO"
        code[8]  = 0x000A214B; // "K!\n\0"
        break;

    case 0xBBBB0002: // "Delayed-receiver"
        // Sleep 500ms
        code[0]  = 0xE30001F4; // MOVW R0, #500
        code[1]  = 0xEF000005; // SVC #0x05 (sleep)
        // Recv
        code[2]  = 0xE3A00000; // MOV R0, #0
        code[3]  = 0xEF000011; // SVC #0x11 (recv)
        code[4]  = 0xE1A04001; // MOV R4, R1 (save payload)
        // Log "RcvOK!\n"
        code[5] = 0xE28F0010; // ADD R0, PC, #16 (PC=code[7], +16=code[11])
        code[6]  = 0xE3A01007; // MOV R1, #7
        code[7]  = 0xEF0000F0; // SVC #0xF0
        // Exit with payload (expect 0x42)
        code[8]  = 0xE1A00004; // MOV R0, R4
        code[9]  = 0xEF000000; // SVC #0x00
        code[10] = 0xEAFFFFFE; // B .
        // DATA at code[11]
        code[11] = 0x4F766352; // "RcvO"
        code[12] = 0x000A214B; // "K!\n\0"
        break;

    /* ================================================================
     * IPC TEST C: Multiple senders, one receiver (FIFO order)
     *   Sender1 sends 0x11, Sender2 sends 0x22 — both block
     *   Receiver sleeps 500ms, then recvs twice
     *   Exit status = first payload (expect 0x11 if FIFO correct)
     * ================================================================ */
    case 0xCCCC0001: // "Multi-sender 1"
        code[0]  = 0xE3A01011; // MOV R1, #0x11
        code[1]  = 0xE3A00000; // MOV R0, #0
        code[2]  = 0xEF000010; // SVC #0x10 (send)
        // Log "Snd1OK\n"
        code[3]  = 0xE28F0008; // ADD R0, PC, #8 (PC=code[5], +8=code[7])
        code[4]  = 0xE3A01007; // MOV R1, #7
        code[5]  = 0xEF0000F0; // SVC #0xF0
        code[6]  = 0xEF000000; // SVC #0x00
        // DATA at code[7]
        code[7]  = 0x31646E53; // "Snd1"
        code[8]  = 0x000A4B4F; // "OK\n\0"
        break;

    case 0xCCCC0002: // "Multi-sender 2"
        code[0]  = 0xE3A01022; // MOV R1, #0x22
        code[1]  = 0xE3A00000; // MOV R0, #0
        code[2]  = 0xEF000010; // SVC #0x10 (send)
        // Log "Snd2OK\n"
        code[3]  = 0xE28F0008; // ADD R0, PC, #8 (PC=code[5], +8=code[7])
        code[4]  = 0xE3A01007; // MOV R1, #7
        code[5]  = 0xEF0000F0; // SVC #0xF0
        code[6]  = 0xEF000000; // SVC #0x00
        // DATA at code[7]
        code[7]  = 0x32646E53; // "Snd2"
        code[8]  = 0x000A4B4F; // "OK\n\0"
        break;

    case 0xCCCC0003: // "Multi-receiver"
        // Sleep 500ms so both senders block first
        code[0]  = 0xE30001F4; // MOVW R0, #500
        code[1]  = 0xEF000005; // SVC #0x05 (sleep)
        // First recv
        code[2]  = 0xE3A00000; // MOV R0, #0
        code[3]  = 0xEF000011; // SVC #0x11
        code[4]  = 0xE1A04001; // MOV R4, R1 (first payload)
        // Second recv
        code[5]  = 0xE3A00000; // MOV R0, #0
        code[6]  = 0xEF000011; // SVC #0x11
        code[7]  = 0xE1A05001; // MOV R5, R1 (second payload)
        // Log "MrcvOK\n"
        code[8] = 0xE28F0010; // ADD R0, PC, #16 (PC=code[10], +16=code[14])
        code[9]  = 0xE3A01007; // MOV R1, #7
        code[10] = 0xEF0000F0; // SVC #0xF0
        // Exit with first payload (expect 0x11 if FIFO)
        code[11] = 0xE1A00004; // MOV R0, R4
        code[12] = 0xEF000000; // SVC #0x00
        code[13] = 0xEAFFFFFE; // B .
        // DATA at code[14]
        code[14] = 0x7663724D; // "Mrcv"
        code[15] = 0x000A4B4F; // "OK\n\0"
        break;

    /* ================================================================
     * IPC TEST D: Invalid handle — should return error, not crash
     * ================================================================ */
    case 0xDDDD0001: // "Bad-handle"
        code[0]  = 0xE3A01042; // MOV R1, #0x42
        code[1]  = 0xE30003E7; // MOVW R0, #999
        code[2]  = 0xEF000010; // SVC #0x10 (send — should fail)
        code[3]  = 0xE1A04000; // MOV R4, R0 (save error)
        // Log "BadH:OK\n"
        code[4]  = 0xE28F0010; // ADD R0, PC, #8 (PC=code[6], +8=code[8])
        code[5]  = 0xE3A01008; // MOV R1, #8
        code[6]  = 0xEF0000F0; // SVC #0xF0
        // Exit with error code (expect ERR_BADARG = -6)
        code[7]  = 0xE1A00004; // MOV R0, R4
        code[8]  = 0xEF000000; // SVC #0x00
        code[9]  = 0xEAFFFFFE; // B .
        // DATA at code[10]
        code[10] = 0x48646142; // "BadH"
        code[11] = 0x0A4B4F3A; // ":OK\n"
        break;

    default: // "The Spinner"
        code[0] = 0xE3A00000; // MOV R0, #0
        code[1] = 0xEAFFFFFE; // B .
        break;
    }

    KDEBUG("Created process with magic %X and PID %d", magic, process->pid);
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
    process_table[p->pid] = NULL;
    kstack_free(p->kernel_stack_top);
    kfree(p);
}
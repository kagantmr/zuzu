#ifndef MEMPROT_H
#define MEMPROT_H

typedef enum {
    VM_PROT_NONE  = 0,
    VM_PROT_READ  = 1u << 0,
    VM_PROT_WRITE = 1u << 1,
    VM_PROT_EXEC  = 1u << 2
} vm_prot_t;

#define VM_PROT_RW ((VM_PROT_READ) | (VM_PROT_WRITE))

#endif
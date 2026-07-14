#ifndef MEMPROT_H
#define MEMPROT_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    VM_PROT_NONE  = 0, // no access
    VM_PROT_READ  = 1u << 0, // read access
    VM_PROT_WRITE = 1u << 1, // write access
    VM_PROT_EXEC  = 1u << 2 // execute access
} vm_prot_t;

#define VM_PROT_RW ((VM_PROT_READ) | (VM_PROT_WRITE))

#ifdef __cplusplus
}
#endif

#endif
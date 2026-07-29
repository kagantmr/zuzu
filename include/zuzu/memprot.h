#ifndef MEMPROT_H
#define MEMPROT_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PROT_NONE  = 0, // no access
    PROT_READ  = 1u << 0, // read access
    PROT_WRITE = 1u << 1, // write access
    PROT_EXEC  = 1u << 2 // execute access
} MemProt;

#define PROT_RW ((PROT_READ) | (PROT_WRITE))

#ifdef __cplusplus
}
#endif

#endif
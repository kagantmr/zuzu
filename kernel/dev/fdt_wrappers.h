#ifndef KERNEL_DEV_FDT_WRAPPERS_H
#define KERNEL_DEV_FDT_WRAPPERS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    char compatible[64];
    uint64_t phys;
    uint64_t size;
    uint64_t phys2;
    uint64_t size2;
    uint32_t irq;
    uint32_t nregs;
} FdtDevice;

bool FdtInit(const void *base);

void FdtEnumerateDevices(void (*cb)(const char * , const char * ,
                                    uint64_t , uint64_t , uint32_t ));

size_t FdtTotalSize(void);

size_t FdtDeviceCount(void);

bool FdtGetReservedMem(uint32_t index, uint64_t *out_addr, uint64_t *out_size);

bool FdtGetReg(const char *path, int index, uint64_t *out_addr, uint64_t *out_size);

bool FdtGetInitrd(uint64_t *out_start, uint64_t *out_end);

bool FdtGetRegPa(const char *path, int index, uint64_t *out_addr, uint64_t *out_size);

const char *FdtModel(void);

const char *FdtCpuCompat(void);

bool FdtTranslateAddressArch(const char *node_path, uint64_t raw_addr, uint64_t *out_phys);
bool FdtResolveIrqArch(const char *node_path, uint32_t child_irq, uint32_t *out_irq,
                       uint32_t *out_flags);

void FdtShutdown(void);

#endif

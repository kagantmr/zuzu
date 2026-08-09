#ifndef ZUZU_FDT_H
#define ZUZU_FDT_H

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

/**
 * Validate the FDT magic.
 */
bool FdtInit(const void *base);

/* Enumerate devices in the DTB without storing a global static table. */
void FdtEnumerateDevices(void (*cb)(const char * /* compatible */, const char * /* path */,
				    uint64_t /* phys */, uint64_t /* size */, uint32_t /* irq */));

/* Total size the FDT blob */
size_t FdtTotalSize(void);

/* Device count from the FDT blob */
size_t FdtDeviceCount(void);

/* Return the reserved memory sections */
bool FdtGetReservedMem(uint32_t index, uint64_t *out_addr, uint64_t *out_size);

/* Return a reg from the FDT */
bool FdtGetReg(const char *path, int index, uint64_t *out_addr, uint64_t *out_size);

/* Get the chosen/ linux,initrd-start/end attributes from the FDT to get the initrd */
bool FdtGetInitrd(uint64_t *out_start, uint64_t *out_end);

bool FdtGetRegPhysAddr(const char *path, int index, uint64_t *out_addr, uint64_t *out_size);

bool FdtFindCompatible(const char *compatible, char *out_path, size_t out_path_cap);

/* Simple string queries */
const char *FdtModel(void);

const char *FdtCpuCompat(void);

/* Arch hooks: arch code can provide stronger implementations by defining
   these symbols (they are weakly referenced in dtb.c). */
bool FdtTranslateAddressArch(const char *node_path, uint64_t raw_addr, uint64_t *out_phys);
bool FdtResolveIrqArch(const char *node_path, uint32_t child_irq, uint32_t *out_irq,
			  uint32_t *out_flags);

/* Shutdown DTB access: clears internal pointer so libfdt won't be used further */
void FdtShutdown(void);

#endif

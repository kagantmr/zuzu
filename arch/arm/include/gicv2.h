#ifndef ARCH_ARM_GICV2_H
#define ARCH_ARM_GICV2_H

#include <stdint.h>
#include <types.h>

#define GICD_CTLR 0x000
#define GICD_IGROUPR 0x080
#define GICD_ISENABLER 0x100
#define GICD_ICENABLER 0x180
#define GICD_ISPENDER 0x200
#define GICD_IPRIORITYR 0x400
#define GICD_ITARGETSR 0x800
#define GICD_ICFGR 0xC00

#define GICC_CTLR 0x000
#define GICC_PMR 0x004
#define GICC_IAR 0x00C
#define GICC_EOIR 0x010

void GicInit(uintptr_t gicd_base_addr, uintptr_t gicc_base_addr);

void GicV2ConfigureIrq(Irq irq_id);

void GicV2UnmaskIrq(Irq irq_id);

void GicV2MaskIrq(uint32_t irq_id);

void GicV2SetPriority(Irq irq_id, uint8_t priority);

uint32_t GicAcknowledge(void);

void GicEnd(uint32_t iar);

#endif

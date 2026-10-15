#ifndef ARCH_ARM_IMPL_ARMV7_MMU_H
#define ARCH_ARM_IMPL_ARMV7_MMU_H

#define MMU_BIT(n) (1U << (n))

#define DESC_TYPE_MASK 0x3U
#define DESC_FAULT 0x0U
#define DESC_L2 0x1U
#define DESC_SECTION 0x2U
#define DESC_SUPERSECTION 0x3U

#define L2_SMALL_TAG MMU_BIT(1)
#define L1_SECTION_TAG DESC_SECTION
#define L1_L2PTR_TAG DESC_L2

#define L1_SECTION_BASE_MASK 0xFFF00000U
#define L1_L2PTR_BASE_MASK 0xFFFFFC00U
#define L2_SMALL_BASE_MASK 0xFFFFF000U

#define MMU_SECTION_SHIFT 20U
#define MMU_PAGE_SHIFT 12U

#define L1_IDX_MASK 0xFFFU
#define L2_IDX_MASK 0xFFU

#define L1_IDX(va) (((va) >> MMU_SECTION_SHIFT) & L1_IDX_MASK)
#define L2_IDX(va) (((va) >> MMU_PAGE_SHIFT) & L2_IDX_MASK)

#define L2_ENTRIES 256U

#define L1_ENTRIES_USER 2048U
#define L1_ENTRIES_KERNEL 4096U
#define L1_DESC_BYTES 4U

#define AP_MASK 0x3U
#define AP_KERNEL_RW 0x1U
#define AP_USER_RO 0x2U
#define AP_USER_RW 0x3U

#define L1_SECT_B_BIT 2U
#define L1_SECT_C_BIT 3U
#define L1_SECT_XN_BIT 4U
#define L1_SECT_AP_SHIFT 10U
#define L1_SECT_TEX_SHIFT 12U
#define L1_SECT_AP2_BIT 15U
#define L1_SECT_S_BIT 16U
#define L1_SECT_NG_BIT 17U

#define L2_PAGE_XN_BIT 0U
#define L2_PAGE_B_BIT 2U
#define L2_PAGE_C_BIT 3U
#define L2_PAGE_AP_SHIFT 4U
#define L2_PAGE_TEX_SHIFT 6U
#define L2_PAGE_AP2_BIT 9U
#define L2_PAGE_S_BIT 10U
#define L2_PAGE_NG_BIT 11U

#define TEX_MASK 0x7U
#define CB_MASK 0x3U

#define TEX_NORMAL_WBWA 0x1U

#define L1_SECT_ATTR_NORMAL                                                                        \
    ((TEX_NORMAL_WBWA << L1_SECT_TEX_SHIFT) | MMU_BIT(L1_SECT_C_BIT) | MMU_BIT(L1_SECT_B_BIT))
#define L1_SECT_ATTR_DEVICE MMU_BIT(L1_SECT_B_BIT)

#define L2_PAGE_ATTR_NORMAL                                                                        \
    ((TEX_NORMAL_WBWA << L2_PAGE_TEX_SHIFT) | MMU_BIT(L2_PAGE_C_BIT) | MMU_BIT(L2_PAGE_B_BIT))
#define L2_PAGE_ATTR_DEVICE MMU_BIT(L2_PAGE_B_BIT)

#define TEX_NORMAL_NC 0x1U

#define L1_SECT_ATTR_NORMAL_NC (TEX_NORMAL_NC << L1_SECT_TEX_SHIFT)

#define L2_PAGE_ATTR_NORMAL_NC (TEX_NORMAL_NC << L2_PAGE_TEX_SHIFT)

#define L2_PAGE_ATTR_MASK                                                                          \
    ((TEX_MASK << L2_PAGE_TEX_SHIFT) | MMU_BIT(L2_PAGE_C_BIT) | MMU_BIT(L2_PAGE_B_BIT))

#define TTBR_IRGN1_BIT 0U
#define TTBR_S_BIT 1U
#define TTBR_RGN_SHIFT 3U
#define TTBR_NOS_BIT 5U
#define TTBR_IRGN0_BIT 6U

#define TTBR_IRGN_WB_NWA 0x3U
#define TTBR_RGN_WBWA 0x1U

#define TTBR_WALK_ATTRS                                                                            \
    (MMU_BIT(TTBR_IRGN1_BIT) | MMU_BIT(TTBR_S_BIT) | (TTBR_RGN_WBWA << TTBR_RGN_SHIFT) |           \
     MMU_BIT(TTBR_NOS_BIT) | MMU_BIT(TTBR_IRGN0_BIT))

#define L1_SECT_BOOT_NORMAL                                                                        \
    (L1_SECTION_TAG | L1_SECT_ATTR_NORMAL | (AP_USER_RW << L1_SECT_AP_SHIFT) |                     \
     MMU_BIT(L1_SECT_S_BIT))

#define L1_SECT_BOOT_DEVICE                                                                        \
    (L1_SECTION_TAG | L1_SECT_ATTR_DEVICE | (AP_USER_RW << L1_SECT_AP_SHIFT) |                     \
     MMU_BIT(L1_SECT_XN_BIT))
    
#ifndef __ASSEMBLER__
_Static_assert(TTBR_WALK_ATTRS == 0x6BU, "TTBR_WALK_ATTRS regressed from the boot value 0x6B");
_Static_assert(((TTBR_WALK_ATTRS >> TTBR_IRGN1_BIT) & 0x1U) == ((TTBR_IRGN_WB_NWA >> 1) & 0x1U) &&
                   ((TTBR_WALK_ATTRS >> TTBR_IRGN0_BIT) & 0x1U) == (TTBR_IRGN_WB_NWA & 0x1U),
               "TTBR IRGN bits must encode TTBR_IRGN_WB_NWA");
_Static_assert(L1_SECT_BOOT_NORMAL == 0x11C0EU, "boot normal section descriptor regressed");
_Static_assert(L1_SECT_BOOT_DEVICE == 0x00C16U, "boot device section descriptor regressed");
#endif /* !__ASSEMBLER__ */

#define SCTLR_M_BIT 0U

#define DACR_DOMAIN0_CLIENT 0x1U

#define TTBCR_N_MASK 0x7U
#define TTBCR_PD0_BIT 4U
#define TTBCR_N_SPLIT_2GB 0x1U

#endif /* ARCH_ARM_IMPL_ARMV7_MMU_H */

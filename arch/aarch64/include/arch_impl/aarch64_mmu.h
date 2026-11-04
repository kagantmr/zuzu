/**
 * @file
 * @brief ARMv7-A short-descriptor translation table formats and MMU register fields.
 */

#ifndef ARCH_ARM_IMPL_ARMV7_MMU_H
#define ARCH_ARM_IMPL_ARMV7_MMU_H

/** @brief Mask with bit @p n set. */
#define MMU_BIT(n) (1U << (n))

/** @brief Type field of an L1 descriptor, bits [1:0]. */
#define DESC_TYPE_MASK 0x3U
/** @brief L1 type: no mapping; any access faults. */
#define DESC_FAULT 0x0U
/** @brief L1 type: points to an L2 page table. */
#define DESC_L2 0x1U
/** @brief L1 type: a 1 MiB section. */
#define DESC_SECTION 0x2U
/** @brief L1 type value 3; zuzu never creates it. */
#define DESC_SUPERSECTION 0x3U

/** @brief L2 type bit for a 4 KiB small page. */
#define L2_SMALL_TAG MMU_BIT(1)
/** @brief Type bits of a section descriptor. */
#define L1_SECTION_TAG DESC_SECTION
/** @brief Type bits of an L1 descriptor that points to an L2 table. */
#define L1_L2PTR_TAG DESC_L2

/** @brief Physical base of a section, bits [31:20]. */
#define L1_SECTION_BASE_MASK 0xFFF00000U
/** @brief Physical base of the L2 table an L1 entry points to, bits [31:10]. */
#define L1_L2PTR_BASE_MASK 0xFFFFFC00U
/** @brief Physical base of a small page, bits [31:12]. */
#define L2_SMALL_BASE_MASK 0xFFFFF000U

/** @brief log2 of the section size. */
#define MMU_SECTION_SHIFT 20U
/** @brief log2 of the page size. */
#define MMU_PAGE_SHIFT 12U

/** @brief Mask of an L1 index (4096 entries). */
#define L1_IDX_MASK 0xFFFU
/** @brief Mask of an L2 index (256 entries). */
#define L2_IDX_MASK 0xFFU

/** @brief L1 index of a virtual address. */
#define L1_IDX(va) (((va) >> MMU_SECTION_SHIFT) & L1_IDX_MASK)
/** @brief L2 index of a virtual address. */
#define L2_IDX(va) (((va) >> MMU_PAGE_SHIFT) & L2_IDX_MASK)

/** @brief Entries in an L2 table, which maps 1 MiB. */
#define L2_ENTRIES 256U

/** @brief Entries in a user (TTBR0) L1 table, covering the lower 2 GiB. */
#define L1_ENTRIES_USER 2048U
/** @brief Entries in a full L1 table, covering 4 GiB. */
#define L1_ENTRIES_KERNEL 4096U
/** @brief Size of one L1 descriptor. */
#define L1_DESC_BYTES 4U

/** @brief Mask of the AP[1:0] access permission field. */
#define AP_MASK 0x3U
/** @brief AP[1:0]: privileged read/write, no user access. */
#define AP_KERNEL_RW 0x1U
/** @brief AP[1:0]: privileged read/write, user read-only. */
#define AP_USER_RO 0x2U
/** @brief AP[1:0]: read/write for both. */
#define AP_USER_RW 0x3U

/** @brief Section bufferable (B) bit. */
#define L1_SECT_B_BIT 2U
/** @brief Section cacheable (C) bit. */
#define L1_SECT_C_BIT 3U
/** @brief Section execute-never bit. */
#define L1_SECT_XN_BIT 4U
/** @brief Shift of a section's AP[1:0]. */
#define L1_SECT_AP_SHIFT 10U
/** @brief Shift of a section's TEX[2:0]. */
#define L1_SECT_TEX_SHIFT 12U
/** @brief Section AP[2] (read-only) bit. */
#define L1_SECT_AP2_BIT 15U
/** @brief Section shareable bit. */
#define L1_SECT_S_BIT 16U
/** @brief Section not-global bit: TLB entries are tagged with the ASID. */
#define L1_SECT_NG_BIT 17U

/** @brief Small page execute-never bit. */
#define L2_PAGE_XN_BIT 0U
/** @brief Small page bufferable (B) bit. */
#define L2_PAGE_B_BIT 2U
/** @brief Small page cacheable (C) bit. */
#define L2_PAGE_C_BIT 3U
/** @brief Shift of a small page's AP[1:0]. */
#define L2_PAGE_AP_SHIFT 4U
/** @brief Shift of a small page's TEX[2:0]. */
#define L2_PAGE_TEX_SHIFT 6U
/** @brief Small page AP[2] (read-only) bit. */
#define L2_PAGE_AP2_BIT 9U
/** @brief Small page shareable bit. */
#define L2_PAGE_S_BIT 10U
/** @brief Small page not-global bit. */
#define L2_PAGE_NG_BIT 11U

/** @brief Mask of a TEX field. */
#define TEX_MASK 0x7U
/** @brief Mask of the C and B bits taken together. */
#define CB_MASK 0x3U

/** @brief TEX value that, with C and B set, means normal write-back write-allocate memory. */
#define TEX_NORMAL_WBWA 0x1U

/** @brief Section memory type: normal write-back write-allocate. */
#define L1_SECT_ATTR_NORMAL                                                                        \
    ((TEX_NORMAL_WBWA << L1_SECT_TEX_SHIFT) | MMU_BIT(L1_SECT_C_BIT) | MMU_BIT(L1_SECT_B_BIT))
/** @brief Section memory type: shareable device. */
#define L1_SECT_ATTR_DEVICE MMU_BIT(L1_SECT_B_BIT)

/** @brief Small page memory type: normal write-back write-allocate. */
#define L2_PAGE_ATTR_NORMAL                                                                        \
    ((TEX_NORMAL_WBWA << L2_PAGE_TEX_SHIFT) | MMU_BIT(L2_PAGE_C_BIT) | MMU_BIT(L2_PAGE_B_BIT))
/** @brief Small page memory type: shareable device. */
#define L2_PAGE_ATTR_DEVICE MMU_BIT(L2_PAGE_B_BIT)

/** @brief TEX value that, with C and B clear, means normal non-cacheable memory. */
#define TEX_NORMAL_NC 0x1U

/** @brief Section memory type: normal non-cacheable. */
#define L1_SECT_ATTR_NORMAL_NC (TEX_NORMAL_NC << L1_SECT_TEX_SHIFT)

/** @brief Small page memory type: normal non-cacheable. */
#define L2_PAGE_ATTR_NORMAL_NC (TEX_NORMAL_NC << L2_PAGE_TEX_SHIFT)

/** @brief Memory-type bits (TEX, C, B) of a small page. */
#define L2_PAGE_ATTR_MASK                                                                          \
    ((TEX_MASK << L2_PAGE_TEX_SHIFT) | MMU_BIT(L2_PAGE_C_BIT) | MMU_BIT(L2_PAGE_B_BIT))

/** @brief TTBR IRGN[1] bit (inner cacheability of table walks). */
#define TTBR_IRGN1_BIT 0U
/** @brief TTBR shareable bit. */
#define TTBR_S_BIT 1U
/** @brief Shift of TTBR RGN (outer cacheability of table walks). */
#define TTBR_RGN_SHIFT 3U
/** @brief TTBR not-outer-shareable bit. */
#define TTBR_NOS_BIT 5U
/** @brief TTBR IRGN[0] bit. */
#define TTBR_IRGN0_BIT 6U

/** @brief IRGN value: inner write-back, no write-allocate. */
#define TTBR_IRGN_WB_NWA 0x3U
/** @brief RGN value: outer write-back write-allocate. */
#define TTBR_RGN_WBWA 0x1U

/** @brief Attributes OR'ed into every TTBR: cacheable, shareable table walks. */
#define TTBR_WALK_ATTRS                                                                            \
    (MMU_BIT(TTBR_IRGN1_BIT) | MMU_BIT(TTBR_S_BIT) | (TTBR_RGN_WBWA << TTBR_RGN_SHIFT) |           \
     MMU_BIT(TTBR_NOS_BIT) | MMU_BIT(TTBR_IRGN0_BIT))

/** @brief Section descriptor the boot code uses for RAM. */
#define L1_SECT_BOOT_NORMAL                                                                        \
    (L1_SECTION_TAG | L1_SECT_ATTR_NORMAL | (AP_USER_RW << L1_SECT_AP_SHIFT) |                     \
     MMU_BIT(L1_SECT_S_BIT))

/** @brief Section descriptor the boot code uses for device memory. */
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

/** @brief SCTLR bit that turns the MMU on. */
#define SCTLR_M_BIT 0U

/** @brief DACR value making domain 0 a client, so permissions are checked. */
#define DACR_DOMAIN0_CLIENT 0x1U

/** @brief Mask of TTBCR.N. */
#define TTBCR_N_MASK 0x7U
/** @brief TTBCR bit that disables TTBR0 table walks. */
#define TTBCR_PD0_BIT 4U
/** @brief TTBCR.N = 1: TTBR0 maps the lower 2 GiB, TTBR1 the upper. */
#define TTBCR_N_SPLIT_2GB 0x1U

#endif /* ARCH_ARM_IMPL_ARMV7_MMU_H */

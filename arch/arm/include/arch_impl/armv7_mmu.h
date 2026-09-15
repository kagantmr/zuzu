/**
 * arch_impl/armv7_mmu.h - ARMv7-A short-descriptor translation table format.
 */

#ifndef ZUZU_ARM_ARMV7_MMU_H
#define ZUZU_ARM_ARMV7_MMU_H

#define MMU_BIT(n) (1U << (n))

/* ---- Descriptor type, bits[1:0] ---------------------------------------- */

#define DESC_TYPE_MASK    0x3U
#define DESC_FAULT        0x0U /* L1 or L2: unmapped, generates a translation fault */
#define DESC_L2           0x1U /* L1: pointer to a 1 KB second-level table */
#define DESC_SECTION      0x2U /* L1: 1 MB section */
#define DESC_SUPERSECTION 0x3U /* L1: section with bit18 set; treated as a section here */

/* Small pages use bit1 as the type tag; bit0 is XN, not part of the type. */
#define L2_SMALL_TAG MMU_BIT(1) /* L2: 4 KB small page */
#define L1_SECTION_TAG DESC_SECTION
#define L1_L2PTR_TAG   DESC_L2

/* ---- Output address fields --------------------------------------------- */

#define L1_SECTION_BASE_MASK 0xFFF00000U /* section PA,   bits[31:20] */
#define L1_L2PTR_BASE_MASK   0xFFFFFC00U /* L2 table PA,  bits[31:10] */
#define L2_SMALL_BASE_MASK   0xFFFFF000U /* small page PA, bits[31:12] */

/* ---- Table geometry ----------------------------------------------------- */

#define MMU_SECTION_SHIFT 20U
#define MMU_PAGE_SHIFT    12U

#define L1_IDX_MASK 0xFFFU /* 4096 entries max */
#define L2_IDX_MASK 0xFFU  /* 256 entries      */

#define L1_IDX(va) (((va) >> MMU_SECTION_SHIFT) & L1_IDX_MASK)
#define L2_IDX(va) (((va) >> MMU_PAGE_SHIFT) & L2_IDX_MASK)

#define L2_ENTRIES 256U /* small-page entries per 1 KB L2 table */

/* With TTBCR.N=1 the TTBR0 (user) L1 covers [0, USER_VA_TOP) with 2048
 * entries; the TTBR1 (kernel) L1 is the full 4096. */
#define L1_ENTRIES_USER   2048U
#define L1_ENTRIES_KERNEL 4096U
#define L1_DESC_BYTES     4U

/* ---- Access permissions, AP[2:0] ---------------------------------------- */
/* AP[2] (the "disable write" bit) stays 0 for every mapping zuzu creates, so
 * only the AP[1:0] field below is ever written. */

#define AP_MASK      0x3U
#define AP_KERNEL_RW 0x1U /* PL1 RW, PL0 no access */
#define AP_USER_RO   0x2U /* PL1 RW, PL0 RO        */
#define AP_USER_RW   0x3U /* PL1 RW, PL0 RW        */

/* ---- Section descriptor field positions --------------------------------- */

#define L1_SECT_B_BIT     2U
#define L1_SECT_C_BIT     3U
#define L1_SECT_XN_BIT    4U
#define L1_SECT_AP_SHIFT  10U
#define L1_SECT_TEX_SHIFT 12U
#define L1_SECT_AP2_BIT   15U
#define L1_SECT_S_BIT     16U
#define L1_SECT_NG_BIT    17U

/* ---- Small-page descriptor field positions ------------------------------ */

#define L2_PAGE_XN_BIT    0U
#define L2_PAGE_B_BIT     2U
#define L2_PAGE_C_BIT     3U
#define L2_PAGE_AP_SHIFT  4U
#define L2_PAGE_TEX_SHIFT 6U
#define L2_PAGE_AP2_BIT   9U
#define L2_PAGE_S_BIT     10U
#define L2_PAGE_NG_BIT    11U

/* ---- Memory-type attributes (TEX[2:0], C, B) ---------------------------- */

#define TEX_MASK 0x7U
#define CB_MASK  0x3U /* C and B together, as they sit adjacent at bits[3:2] */

#define TEX_NORMAL_WBWA 0x1U /* TEX=0b001 with C=1,B=1: Normal Write-Back Write-Allocate */
#define TEX_DEVICE      0x0U /* TEX=0b000 with C=0,B=1: Device                          */

/* Attribute words pre-positioned for each descriptor format. */
#define L1_SECT_ATTR_NORMAL                                                                        \
    ((TEX_NORMAL_WBWA << L1_SECT_TEX_SHIFT) | MMU_BIT(L1_SECT_C_BIT) | MMU_BIT(L1_SECT_B_BIT))
#define L1_SECT_ATTR_DEVICE MMU_BIT(L1_SECT_B_BIT)

#define L2_PAGE_ATTR_NORMAL                                                                        \
    ((TEX_NORMAL_WBWA << L2_PAGE_TEX_SHIFT) | MMU_BIT(L2_PAGE_C_BIT) | MMU_BIT(L2_PAGE_B_BIT))
#define L2_PAGE_ATTR_DEVICE MMU_BIT(L2_PAGE_B_BIT)

/* Covers TEX, C and B -- everything that identifies the memory type. */
#define L2_PAGE_ATTR_MASK                                                                          \
    ((TEX_MASK << L2_PAGE_TEX_SHIFT) | MMU_BIT(L2_PAGE_C_BIT) | MMU_BIT(L2_PAGE_B_BIT))

/* ---- TTBR0 / TTBR1 walk attributes, bits[13:0] -------------------------- */

#define TTBR_IRGN1_BIT 0U
#define TTBR_S_BIT     1U
#define TTBR_RGN_SHIFT 3U /* RGN[1:0], bits[4:3] */
#define TTBR_NOS_BIT   5U
#define TTBR_IRGN0_BIT 6U

#define TTBR_IRGN_WB_NWA 0x3U /* inner Write-Back, no Write-Allocate */
#define TTBR_RGN_WBWA    0x1U /* outer Write-Back Write-Allocate     */

#define TTBR_WALK_ATTRS                                                                            \
    (MMU_BIT(TTBR_IRGN1_BIT) | MMU_BIT(TTBR_S_BIT) | (TTBR_RGN_WBWA << TTBR_RGN_SHIFT) |           \
     MMU_BIT(TTBR_NOS_BIT) | MMU_BIT(TTBR_IRGN0_BIT))

/* ---- Boot-path section descriptors -------------------------------------- */
#define L1_SECT_BOOT_NORMAL                                                                        \
    (L1_SECTION_TAG | L1_SECT_ATTR_NORMAL | (AP_USER_RW << L1_SECT_AP_SHIFT) |                     \
     MMU_BIT(L1_SECT_S_BIT))

#define L1_SECT_BOOT_DEVICE                                                                        \
    (L1_SECTION_TAG | L1_SECT_ATTR_DEVICE | (AP_USER_RW << L1_SECT_AP_SHIFT) |                     \
     MMU_BIT(L1_SECT_XN_BIT))

/* ---- Encoding self-checks ----------------------------------------------- */


#ifndef __ASSEMBLER__
_Static_assert(TTBR_WALK_ATTRS == 0x6BU, "TTBR_WALK_ATTRS regressed from the boot value 0x6B");
_Static_assert(((TTBR_WALK_ATTRS >> TTBR_IRGN1_BIT) & 0x1U) == ((TTBR_IRGN_WB_NWA >> 1) & 0x1U) &&
                   ((TTBR_WALK_ATTRS >> TTBR_IRGN0_BIT) & 0x1U) == (TTBR_IRGN_WB_NWA & 0x1U),
               "TTBR IRGN bits must encode TTBR_IRGN_WB_NWA");
_Static_assert(L1_SECT_BOOT_NORMAL == 0x11C0EU, "boot normal section descriptor regressed");
_Static_assert(L1_SECT_BOOT_DEVICE == 0x00C16U, "boot device section descriptor regressed");
#endif /* !__ASSEMBLER__ */

/* ---- System control registers ------------------------------------------- */

#define SCTLR_M_BIT 0U /* MMU enable */

#define DACR_DOMAIN0_CLIENT 0x1U /* domain 0 = Client: use descriptor permissions */

#define TTBCR_N_MASK   0x7U /* bits[2:0]: TTBR0 size / split point */
#define TTBCR_PD0_BIT  4U
#define TTBCR_PD1_BIT  5U
#define TTBCR_N_SPLIT_2GB 0x1U /* N=1: TTBR0 covers [0, 0x80000000) */

#endif /* ZUZU_ARM_ARMV7_MMU_H */

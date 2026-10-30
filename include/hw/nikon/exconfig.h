/*
 * EX class-factory configuration composer.
 *
 * The supplied EX component has an erased factory configuration window
 * (0x08004000..0x08020000 is all zero in the C1.70 update payload and in the
 * assembled container).  The firmware nevertheless consumes three adjacent
 * configuration blocks on the cold-start path, so the machine model supplies a
 * synthetic structure instead of a recovered factory dump.
 *
 * Block format, from the firmware itself:
 *
 *   +0x00  4-byte ASCII magic: "MAKE", "MODE", "INDI"
 *   +0x04  u32 total block size, including this header and the trailing echo
 *   +0x10  records, back to back
 *   end-4  u32 echo of the size field
 *
 * `sub_802ED04` accepts a block only when the magic matches and the echo equals
 * the size field; `sub_80294B0` compares the first four bytes of the primary
 * block against "MAKE" to select the factory branch.  The record boundaries
 * below are not stored in flash: they come from the firmware's own nine-entry
 * record table (20 bytes each, base 0x2000E79C), which the cold boot fills from
 * a compressed ROM image.  Each record occupies
 * {offset within block, length} as listed here, and every block's size equals
 * 16 + the sum of its record lengths + 4.
 *
 * Field consumers matched to the record byte offsets we write:
 *   record 1 +276  -> 0x2001F644, configured-path channel 0    (sub_8029418)
 *   record 1 +278  -> 0x2001F646 mode word low/high byte       (sub_8029418)
 *   record 1 +279  -> mode byte compared against 5; 5 arms the
 *                     0x2000ED90 setup gate via sub_802B40C    (sub_8029418)
 *   record 1 +541  -> 0x200138D6, power-rail monitor gate      (sub_8032C3C,
 *                                                              sub_80426B8)
 *
 * This is model data for the bundle board record: a class-factory structure
 * that the original firmware's own validators accept, not recovered Nikon
 * factory calibration.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef HW_NIKON_EXCONFIG_H
#define HW_NIKON_EXCONFIG_H

#include "qemu/osdep.h" /* IWYU pragma: keep */

/* Primary view at 0x08004000: "MAKE", one 0x0e18-byte record. */
#define NIKON_EX_CONFIG_A_OFFSET 0x4000u
#define NIKON_EX_CONFIG_A_SIZE 0x0e2cu
#define NIKON_EX_CONFIG_REC1_OFFSET 0x0010u
#define NIKON_EX_CONFIG_REC1_SIZE 0x0e18u

/* Second view at 0x08008000: "MODE", four records 3..6. */
#define NIKON_EX_CONFIG_B_OFFSET 0x8000u
#define NIKON_EX_CONFIG_B_SIZE 0x11bcu
#define NIKON_EX_CONFIG_REC3_OFFSET 0x0010u
#define NIKON_EX_CONFIG_REC3_SIZE 0x00f0u
#define NIKON_EX_CONFIG_REC4_OFFSET 0x0100u
#define NIKON_EX_CONFIG_REC4_SIZE 0x0200u
#define NIKON_EX_CONFIG_REC5_OFFSET 0x0300u
#define NIKON_EX_CONFIG_REC5_SIZE 0x0040u
#define NIKON_EX_CONFIG_REC6_OFFSET 0x0340u
#define NIKON_EX_CONFIG_REC6_SIZE 0x0e78u

/* Third view at 0x0800c000: "INDI", one four-byte record 8. */
#define NIKON_EX_CONFIG_C_OFFSET 0xc000u
#define NIKON_EX_CONFIG_C_SIZE 0x0018u
#define NIKON_EX_CONFIG_REC8_OFFSET 0x0010u
#define NIKON_EX_CONFIG_REC8_SIZE 0x0004u

#define NIKON_EX_CONFIG_HEADER_SIZE 0x0010u

/* Record-1 fields written by the composer, relative to the record start. */
#define NIKON_EX_CONFIG_REC1_CHANNEL0 276u
#define NIKON_EX_CONFIG_REC1_MODE 278u
#define NIKON_EX_CONFIG_REC1_GATE 541u

/* Every block and its trailing echo must fit inside the flash image. */
#define NIKON_EX_CONFIG_SPAN \
    (NIKON_EX_CONFIG_C_OFFSET + NIKON_EX_CONFIG_C_SIZE)

/*
 * Write the three synthetic configuration blocks into `flash`.  Each block is
 * erased first, so fields this model does not supply keep erased zero.  Bytes
 * outside the three blocks are not touched.  Returns false (writing nothing)
 * when `flash` cannot hold the complete span.
 */
bool nikon_ex_config_compose(uint8_t *flash, size_t flash_size);

#endif /* HW_NIKON_EXCONFIG_H */

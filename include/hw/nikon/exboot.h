/*
 * EX cold-boot scatter loader.
 *
 * The original reset vector only relocates the vector table; the data segment
 * is produced by a boot loader that walks sixteen-byte records
 * {source, destination, length, loader}.  This image uses the zero fill
 * (0x080038b0) and a byte-oriented LZ decoder (0x08003838).  The third loader
 * (0x08003894) is not implemented yet and stays an explicit gap.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef HW_NIKON_EXBOOT_H
#define HW_NIKON_EXBOOT_H

#include "qemu/osdep.h" /* IWYU pragma: keep */

#define NIKON_EX_SCATTER_TABLE 0x080038cc
#define NIKON_EX_SCATTER_LZ 0x08003838
#define NIKON_EX_SCATTER_ZERO 0x080038b0
#define NIKON_EX_SCATTER_UNSUPPORTED 0x08003894

/*
 * Apply the scatter table at `table` (an address inside `flash`) to `ram`.
 * Both spans are bounds-checked before any write; a malformed record stops the
 * walk and returns false.  Records with the unsupported loader are skipped, so
 * their destination keeps its reset value and the gap stays visible.
 */
bool nikon_ex_boot_image(uint8_t *ram, uint32_t ram_base, size_t ram_size,
                         const uint8_t *flash, uint32_t flash_base,
                         size_t flash_size, uint32_t table);

#endif

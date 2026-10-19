/*
 * EX cold-boot scatter loader.
 *
 * The original reset vector at 0x080002b4 only relocates the vector table and
 * sets VTOR; the data segment comes from a boot loader that walks the
 * sixteen-byte records at 0x080038cc.  Without that step the pin and driver
 * descriptor image at 0x2000d000 is missing, so original code reads zeroed
 * descriptors and branches through garbage pointers.
 *
 * Record layout: {source, destination, length, loader}.  This image uses the
 * zero fill at 0x080038b0 and the LZ decoder at 0x08003838; the loader at
 * 0x08003894 (two diagnostic images at 0x2003c000 and 0x2003f000) is not
 * implemented and is skipped explicitly.
 *
 * The decoder is the original algorithm: a control byte splits into a literal
 * count (low three bits, extended byte when zero) and a run count (high
 * nibble, extended byte when zero).  Bit 3 selects a back reference of run+2
 * bytes at a one-byte distance, otherwise a run of zero bytes.  Both extended
 * bytes precede the literal bytes.  Literal bytes copied are count-1, so an
 * encoded count of 1 emits nothing.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "hw/nikon/exboot.h"

#define EX_SCATTER_ENTRY 16

static bool nikon_ex_lz_decode(uint8_t *dst, size_t out_len, const uint8_t *src,
                               const uint8_t *src_end)
{
    uint8_t *const start = dst;
    uint8_t *const end = dst + out_len;

    while (dst < end) {
        uint8_t control;
        unsigned literals, run, i;

        if (src >= src_end) {
            return false;
        }
        control = *src++;
        literals = control & 7;
        run = control >> 4;
        if (!literals) {
            if (src >= src_end) {
                return false;
            }
            literals = *src++;
        }
        if (!run) {
            if (src >= src_end) {
                return false;
            }
            run = *src++;
        }
        for (i = 1; i < literals && dst < end; i++) {
            if (src >= src_end) {
                return false;
            }
            *dst++ = *src++;
        }
        if (control & 8) {
            unsigned distance;
            const uint8_t *from;

            if (src >= src_end) {
                return false;
            }
            distance = *src++;
            if (distance > (unsigned)(dst - start)) {
                return false;
            }
            from = dst - distance;
            for (i = 0; i < run + 2 && dst < end; i++) {
                *dst++ = *from++;
            }
        } else {
            for (i = 0; i < run && dst < end; i++) {
                *dst++ = 0;
            }
        }
    }
    return true;
}

bool nikon_ex_boot_image(uint8_t *ram, uint32_t ram_base, size_t ram_size,
                         const uint8_t *flash, uint32_t flash_base,
                         size_t flash_size, uint32_t table)
{
    size_t off;

    if (!ram || !flash || table < flash_base) {
        return false;
    }
    off = table - flash_base;
    while (off + EX_SCATTER_ENTRY <= flash_size) {
        uint32_t src = ldl_le_p(flash + off);
        uint32_t dst = ldl_le_p(flash + off + 4);
        uint32_t len = ldl_le_p(flash + off + 8);
        uint32_t fn = ldl_le_p(flash + off + 12);
        size_t src_off, dst_off;

        off += EX_SCATTER_ENTRY;
        if (!src || !dst || !len || !fn) {
            break;
        }
        if (src < flash_base || dst < ram_base) {
            return false;
        }
        src_off = src - flash_base;
        dst_off = dst - ram_base;
        if (src_off >= flash_size || dst_off >= ram_size ||
            len > ram_size - dst_off) {
            return false;
        }
        if (fn == NIKON_EX_SCATTER_ZERO) {
            memset(ram + dst_off, 0, len);
        } else if (fn == NIKON_EX_SCATTER_LZ) {
            if (!nikon_ex_lz_decode(ram + dst_off, len, flash + src_off,
                                    flash + flash_size)) {
                return false;
            }
        }
        /* NIKON_EX_SCATTER_UNSUPPORTED and unknown loaders stay untouched. */
    }
    return true;
}

/*
 * The panel observer's console conversion is a byte reorder only: guest pixels
 * are neither interpreted nor filtered.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "hw/nikon/panel.h"

int (*qemu_main)(void);

static void test_convert_opaque(void)
{
    const uint8_t source[8] = {
        0x11, 0x22, 0x33, 0xff, /* R G B A */
        0x00, 0x00, 0x00, 0x00, /* transparent black */
    };
    uint32_t destination[2];

    nikon_panel_convert_rgba8888(destination, source, 2);
    /* 0xAARRGGBB words: A from byte 3, R from byte 0, G from 1, B from 2. */
    g_assert_cmphex(destination[0], ==, 0xff112233u);
    g_assert_cmphex(destination[1], ==, 0x00000000u);
}

static void test_convert_alpha(void)
{
    const uint8_t source[4] = { 0xff, 0x80, 0x01, 0x80 };
    uint32_t destination[1];

    nikon_panel_convert_rgba8888(destination, source, 1);
    /* Alpha is copied unchanged; no premultiplication is performed here. */
    g_assert_cmphex(destination[0], ==, 0x80ff8001u);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/nikon/panel/rgba-convert", test_convert_opaque);
    g_test_add_func("/nikon/panel/alpha-convert", test_convert_alpha);
    return g_test_run();
}

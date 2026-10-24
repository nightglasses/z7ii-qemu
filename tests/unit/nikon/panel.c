/*
 * The panel observer's console conversion is a byte reorder only: guest pixels
 * are neither interpreted nor filtered.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/rcu.h"
#include "qemu/timer.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/sysbus.h"
#include "hw/nikon/panel.h"
#include "system/address-spaces.h"
#include "system/system.h"
#include "ui/console.h"
#include "ui/surface.h"

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

typedef struct PanelListener {
    DisplayChangeListener dcl;
    unsigned updates;
} PanelListener;

static void frame_update(DisplayChangeListener *dcl,
                         int x, int y, int width, int height)
{
    PanelListener *listener = container_of(dcl, PanelListener, dcl);

    g_assert_cmpint(x, ==, 0);
    g_assert_cmpint(y, ==, 0);
    g_assert_cmpint(width, ==, 1024);
    g_assert_cmpint(height, ==, 2);
    listener->updates++;
}

static const DisplayChangeListenerOps listener_ops = {
    .dpy_name = "nikon-panel-test",
    .dpy_gfx_update = frame_update,
};

static void advance(void)
{
    qemu_clock_advance_virtual_time(qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                                    100000);
}

static void assert_frame(QemuConsole *console, const uint8_t *source)
{
    DisplaySurface *surface = qemu_console_surface(console);
    const uint8_t *pixels = surface_data(surface);

    g_assert_cmpint(surface_width(surface), ==, 1024);
    g_assert_cmpint(surface_height(surface), ==, 2);
    for (unsigned y = 0; y < 2; y++) {
        for (unsigned x = 0; x < 1024; x++) {
            const uint8_t *rgba = source + ((y + 1) * 1024 + x) * 4;
            uint32_t actual;
            uint32_t expected = ((uint32_t)rgba[3] << 24) |
                                ((uint32_t)rgba[0] << 16) |
                                ((uint32_t)rgba[1] << 8) | rgba[2];

            memcpy(&actual, pixels + y * surface_stride(surface) + x * 4, 4);
            g_assert_cmphex(actual, ==, expected);
        }
    }
}

static void test_frame_changes(void)
{
    DeviceState *dev = qdev_new(TYPE_NIKON_PANEL);
    MemoryRegion ram, partial;
    MemoryRegion *root = get_system_memory();
    PanelListener listener = { 0 };
    QemuConsole *console;
    uint8_t *source;
    uint8_t before[3 * 1024 * 4];

    object_property_add_child(object_get_objects_root(), "panel[*]",
                              OBJECT(dev));
    memory_region_init_ram_flags_nomigrate(&ram, OBJECT(dev),
                                          "test.panel", sizeof(before), 0,
                                          &error_abort);
    memory_region_add_subregion(root, 0x100000, &ram);
    memory_region_init_alias(&partial, OBJECT(dev), "test.panel-partial", &ram,
                             0, 2 * 1024 * 4);
    source = memory_region_get_ram_ptr(&ram);
    for (unsigned i = 0; i < sizeof(before) / 4; i++) {
        memcpy(source + i * 4, (uint8_t[]) { 0x11, 0x22, 0x33, 0xff }, 4);
    }
    memset(source, 0xab, 1024 * 4);
    memcpy(before, source, sizeof(before));
    qdev_prop_set_uint64(dev, "base", 0x100000);
    qdev_prop_set_uint32(dev, "width", 1024);
    qdev_prop_set_uint32(dev, "height", 3);
    qdev_prop_set_uint32(dev, "skip-rows", 1);
    qdev_prop_set_uint64(dev, "interval-ns", 100000);
    g_assert_true(sysbus_realize(SYS_BUS_DEVICE(dev), &error_abort));
    g_assert_null(qemu_console_lookup_by_device(dev, 0));
    advance();
    console = qemu_console_lookup_by_device(dev, 0);
    g_assert_nonnull(console);
    assert_frame(console, source);
    g_assert_cmpmem(source, sizeof(before), before, sizeof(before));
    qemu_console_register_listener(console, &listener.dcl, &listener_ops);
    listener.updates = 0;

    /* Keep each row's first pixel fixed: the old sparse hash misses both. */
    source[1024 * 4 + 4] = 0x55;
    advance();
    g_assert_cmpuint(listener.updates, ==, 1);
    assert_frame(console, source);
    source[sizeof(before) - 4] = 0x77;
    advance();
    g_assert_cmpuint(listener.updates, ==, 2);
    assert_frame(console, source);
    advance();
    g_assert_cmpuint(listener.updates, ==, 2);

    /* Inactive rows cannot trigger publication or change visible pixels. */
    memset(source, 0xcd, 1024 * 4);
    advance();
    g_assert_cmpuint(listener.updates, ==, 2);
    assert_frame(console, source);

    /* A partial mapping retains the whole last frame, then retries normally. */
    memcpy(before, source, sizeof(before));
    source[1024 * 4] = 0x88;
    memory_region_del_subregion(root, &ram);
    memory_region_add_subregion(root, 0x100000, &partial);
    advance();
    g_assert_cmpuint(listener.updates, ==, 2);
    assert_frame(console, before);
    memory_region_del_subregion(root, &partial);
    memory_region_add_subregion(root, 0x100000, &ram);
    advance();
    g_assert_cmpuint(listener.updates, ==, 3);
    assert_frame(console, source);
    memcpy(before, source, sizeof(before));
    advance();
    g_assert_cmpuint(listener.updates, ==, 3);
    g_assert_cmpmem(source, sizeof(before), before, sizeof(before));
    device_cold_reset(dev);
    advance();
    g_assert_cmpuint(listener.updates, ==, 3);
    assert_frame(console, source);

    qemu_console_unregister_listener(&listener.dcl);
    qdev_unrealize(dev);
    g_assert_null(qemu_console_lookup_by_device(dev, 0));
    /* No console callback or scheduled scan may retain the freed device. */
    qemu_console_hw_update(console);
    advance();
    object_property_set_bool(OBJECT(dev), "realized", true, &error_abort);
    g_assert_true(qdev_is_realized(dev));
    g_assert_null(qemu_console_lookup_by_device(dev, 0));
    advance();
    console = qemu_console_lookup_by_device(dev, 0);
    g_assert_nonnull(console);
    assert_frame(console, source);
    qdev_unrealize(dev);
    g_assert_null(qemu_console_lookup_by_device(dev, 0));
    memory_region_del_subregion(root, &ram);
    drain_call_rcu();
    object_unparent(OBJECT(dev));
    object_unref(OBJECT(dev));
    drain_call_rcu();
    advance();
}

int main(int argc, char **argv)
{
    g_autofree char *directory = g_dir_make_tmp("nikonXXXXXX", NULL);
    g_autofree char *socket = g_build_filename(directory, "qtest", NULL);
    g_autofree char *chardev = g_strdup_printf("unix:%s,server=on,wait=off",
                                             socket);
    const char *const arguments[] = {
        argv[0], "-machine", "none", "-accel", "qtest", "-display", "none",
        "-nodefaults", "-qtest", chardev, NULL
    };
    g_auto(GStrv) qemu_argv = g_new0(char *, G_N_ELEMENTS(arguments));
    int result;

    g_assert_nonnull(directory);
    g_test_init(&argc, &argv, NULL);
    for (unsigned i = 0; i < G_N_ELEMENTS(arguments) - 1; i++) {
        qemu_argv[i] = g_strdup(arguments[i]);
    }
    qemu_init(G_N_ELEMENTS(arguments) - 1, qemu_argv);
    g_test_add_func("/nikon/panel/rgba-convert", test_convert_opaque);
    g_test_add_func("/nikon/panel/alpha-convert", test_convert_alpha);
    g_test_add_func("/nikon/panel/frame-changes", test_frame_changes);
    result = g_test_run();
    qemu_cleanup(result);
    unlink(socket);
    rmdir(directory);
    return result;
}

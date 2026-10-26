/* Real upstream GPIO receivers and global three-phase reset registration.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/core/clock.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-clock.h"
#include "hw/core/qdev-properties.h"
#include "hw/gpio/stm32l4x5_gpio.h"
#include "hw/nikon/input.h"
#include "standard-headers/linux/input-event-codes.h"
#include "system/reset.h"
#include "system/runstate.h"
#include "system/system.h"
#include "ui/input.h"

int (*qemu_main)(void);

static const uint8_t pads[] = {
    0x0a, 0x11, 0x1f, 0x25, 0x1b, 0x47, 0x4d, 0x5b, 0x5e,
    0x61, 0x62, 0x6d, 0x6e, 0x6f, 0x1d, 0x53, 0x1c,
};

static const unsigned int keys[] = {
    KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6,
    KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12,
    KEY_1, KEY_2, KEY_3, KEY_4, KEY_5,
};

typedef struct InputFixture {
    Object *owner;
    Clock *clock;
    DeviceState *input;
    DeviceState *gpio[7];
    qemu_irq selector;
    int selector_level;
    unsigned selector_changes;
    unsigned int cycle;
} InputFixture;

static void selector_level(void *opaque, int pin, int level)
{
    InputFixture *f = opaque;

    if (f->selector_level != level) {
        f->selector_changes++;
    }
    f->selector_level = level;
}

static InputFixture *input_new(void)
{
    InputFixture *f = g_new0(InputFixture, 1);

    f->owner = object_new(TYPE_CONTAINER);
    object_property_add_child(object_get_objects_root(), "input[*]", f->owner);
    f->clock = clock_new(f->owner, "clock");
    clock_set_hz(f->clock, 1000000); /* Fixture clock, no camera frequency claim. */
    f->input = qdev_new(TYPE_NIKON_INPUT);
    object_property_add_child(f->owner, "frontend", OBJECT(f->input));
    f->selector_level = -1;
    f->selector = qemu_allocate_irq(selector_level, f, 0);
    qdev_connect_gpio_out_named(f->input, "capture-selector", 0, f->selector);
    for (unsigned i = 0; i < ARRAY_SIZE(f->gpio); i++) {
        g_autofree char *name = g_strdup_printf("test.port%u", i);

        f->gpio[i] = qdev_new(TYPE_STM32L4X5_GPIO);
        /* QOM links resolve canonical paths. Root the receiver before wiring
         * its IRQ child; an unrooted GPIO's child resolves to an empty link.
         */
        object_property_add_child(f->owner, name, OBJECT(f->gpio[i]));
        qdev_prop_set_string(f->gpio[i], "name", name);
        qdev_prop_set_uint32(f->gpio[i], "mode-reset", 0);
        qdev_prop_set_uint32(f->gpio[i], "pupd-reset", 0);
        qdev_connect_clock_in(f->gpio[i], "clk", f->clock);
    }
    for (unsigned i = 0; i < ARRAY_SIZE(pads); i++) {
        qemu_irq line = qdev_get_gpio_in(f->gpio[pads[i] >> 4], pads[i] & 15);

        qdev_connect_gpio_out_named(f->input, "pad", i,
                                    line);
        g_assert_true(qdev_get_gpio_out_connector(f->input, "pad", i) == line);
    }
    g_assert_true(qdev_realize(f->input, NULL, &error_abort));
    /* Real SysBus receivers are already reachable through their bus reset
     * tree. Direct region access needs no mapping or private-AS alias.
     */
    for (unsigned i = 0; i < ARRAY_SIZE(f->gpio); i++) {
        g_assert_true(sysbus_realize(SYS_BUS_DEVICE(f->gpio[i]), &error_abort));
    }
    return f;
}

static void input_free(InputFixture *f)
{
    qdev_unrealize(f->input);
    g_assert_cmpint(f->selector_level, ==, -1);
    object_unparent(OBJECT(f->input));
    object_unref(OBJECT(f->input));
    for (unsigned i = 0; i < ARRAY_SIZE(f->gpio); i++) {
        qdev_unrealize(f->gpio[i]);
        object_unparent(OBJECT(f->gpio[i]));
        object_unref(OBJECT(f->gpio[i]));
    }
    object_unparent(f->owner);
    object_unref(f->owner);
    qemu_free_irq(f->selector);
    g_free(f);
}

static void levels(InputFixture *f, uint32_t down, const char *phase)
{
    for (unsigned i = 0; i < ARRAY_SIZE(pads); i++) {
        MemoryRegion *regs = sysbus_mmio_get_region(
            SYS_BUS_DEVICE(f->gpio[pads[i] >> 4]), 0);
        uint64_t idr;

        g_assert_cmpint(memory_region_dispatch_read(regs, 0x10, &idr, MO_32,
                            MEMTXATTRS_UNSPECIFIED), ==, MEMTX_OK);
        if (((idr >> (pads[i] & 15)) & 1) != !(down & (1U << i))) {
            g_test_message("cycle=%u phase=%s pad=%u id=%02x IDR=%08" PRIx64
                           " down=%08x input-reset=%d gpio-reset=%d",
                           f->cycle, phase, i, pads[i], idr, down,
                           device_is_in_reset(f->input),
                           device_is_in_reset(f->gpio[pads[i] >> 4]));
        }
        g_assert_cmpint((idr >> (pads[i] & 15)) & 1, ==,
                        !(down & (1U << i)));
    }
}

static void key(unsigned int code, bool down)
{
    qemu_input_event_send_key_linux(NULL, code, down);
}

static void board_reset(void)
{
    qemu_system_reset(SHUTDOWN_CAUSE_HOST_QMP_SYSTEM_RESET);
}

static void receiver_holds_after_frontend(InputFixture *f)
{
    /* Use the standard reset API to reproduce the ordering that erased the
     * old frontend hold publication. No phase callback is invoked directly.
     */
    resettable_assert_reset(OBJECT(f->input), RESET_TYPE_COLD);
    for (unsigned i = 0; i < ARRAY_SIZE(f->gpio); i++) {
        resettable_assert_reset(OBJECT(f->gpio[i]), RESET_TYPE_COLD);
    }
    for (unsigned i = 0; i < ARRAY_SIZE(f->gpio); i++) {
        resettable_release_reset(OBJECT(f->gpio[i]), RESET_TYPE_COLD);
    }
    resettable_release_reset(OBJECT(f->input), RESET_TYPE_COLD);
}

static void test_receivers(void)
{
    for (unsigned cycle = 0; cycle < 2; cycle++) {
        InputFixture *f = input_new();

        f->cycle = cycle;
        board_reset();
        levels(f, 0, "released-after-global-reset");
        for (unsigned i = 0; i < ARRAY_SIZE(keys); i++) {
            key(keys[i], true);
            levels(f, 1U << i, "press");
            key(keys[i], true);
            levels(f, 1U << i, "repeat");
            key(keys[i], false);
            levels(f, 0, "release");
        }
        key(KEY_F1, true);
        key(KEY_5, true);
        levels(f, (1U << 0) | (1U << 16), "held-before-reset");
        receiver_holds_after_frontend(f);
        levels(f, (1U << 0) | (1U << 16), "held-after-late-receiver-hold");
        board_reset();
        levels(f, (1U << 0) | (1U << 16), "held-after-global-reset");
        key(KEY_ESC, true);
        key(KEY_ESC, false);
        levels(f, (1U << 0) | (1U << 16), "unmapped-key");
        key(KEY_F1, false);
        levels(f, 1U << 16, "release-first-held");
        key(KEY_5, false);
        levels(f, 0, "release-last-held");
        input_free(f);
        /* Finalized frontends must not remain in reset or input registries. */
        board_reset();
        key(KEY_F1, true);
        key(KEY_F1, false);
    }
}

static void test_selector(void)
{
    for (unsigned cycle = 0; cycle < 2; cycle++) {
        InputFixture *f = input_new();
        unsigned changes;

        g_assert_cmpint(f->selector_level, ==, 1);
        board_reset();
        g_assert_cmpint(f->selector_level, ==, 1);
        changes = f->selector_changes;
        key(KEY_F13, false);
        key(KEY_ESC, true);
        key(KEY_ESC, false);
        g_assert_cmpuint(f->selector_changes, ==, changes);
        key(KEY_F13, true);
        g_assert_cmpint(f->selector_level, ==, 0);
        g_assert_cmpuint(f->selector_changes, ==, changes + 1);
        key(KEY_F13, true);
        receiver_holds_after_frontend(f);
        board_reset();
        key(KEY_F13, true);
        g_assert_cmpint(f->selector_level, ==, 0);
        g_assert_cmpuint(f->selector_changes, ==, changes + 1);
        key(KEY_F13, false);
        g_assert_cmpint(f->selector_level, ==, 0);
        key(KEY_F13, true);
        g_assert_cmpint(f->selector_level, ==, 1);
        key(KEY_F13, false);
        levels(f, 0, "selector-does-not-change-GPIO-scan-pads");
        input_free(f);
        board_reset();
        key(KEY_F13, true);
        key(KEY_F13, false);
    }
}

int main(int argc, char **argv)
{
    g_autofree char *directory = g_dir_make_tmp("nikonXXXXXX", NULL);
    g_autofree char *socket = g_build_filename(directory, "qtest", NULL);
    g_autofree char *chardev = g_strdup_printf("unix:%s,server=on,wait=off",
                                             socket);
    char *qemu_argv[] = {
        argv[0], "-machine", "none", "-accel", "qtest", "-display", "none",
        "-nodefaults", "-qtest", chardev, NULL
    };
    int result;

    g_assert_nonnull(directory);
    g_test_init(&argc, &argv, NULL);
    qemu_init(G_N_ELEMENTS(qemu_argv) - 1, qemu_argv);
    vm_start();
    g_test_add_func("/nikon/input/receivers-reset-lifecycle", test_receivers);
    g_test_add_func("/nikon/input/capture-selector", test_selector);
    result = g_test_run();
    qemu_cleanup(result);
    unlink(socket);
    rmdir(directory);
    return result;
}

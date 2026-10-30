/* Real upstream GPIO receivers and global three-phase reset registration.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/timer.h"
#include "qapi/error.h"
#include "hw/core/clock.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-clock.h"
#include "hw/core/qdev-properties.h"
#include "hw/gpio/stm32l4x5_gpio.h"
#include "hw/nikon/input.h"
#include "standard-headers/linux/input-event-codes.h"
#include "system/reset.h"
#include "system/cpu-timers.h"
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

static const uint8_t expander_pads[] = {
    9, 3, 8, 23, 2, 22, 21, 6, 7, 5, 4, 14, 15, 13, 12, 11,
};

static const unsigned int expander_keys[] = {
    KEY_F20, KEY_ENTER, KEY_F21, KEY_DELETE, KEY_I, KEY_EQUAL, KEY_MINUS,
    KEY_RIGHT, KEY_LEFT, KEY_UP, KEY_DOWN, KEY_D, KEY_A, KEY_W, KEY_S, KEY_F22,
};

typedef struct InputFixture {
    Object *owner;
    Clock *clock;
    DeviceState *input;
    DeviceState *gpio[7];
    qemu_irq expander[16];
    int expander_level[16];
    unsigned expander_changes[16];
    qemu_irq selector;
    qemu_irq mode;
    int mode_level;
    unsigned mode_changes;
    int selector_level;
    unsigned selector_changes;
    unsigned int cycle;
} InputFixture;

static void expander_level(void *opaque, int pin, int level)
{
    InputFixture *f = opaque;

    if (f->expander_level[pin] != level) {
        f->expander_changes[pin]++;
    }
    f->expander_level[pin] = level;
}

static void mode_level(void *opaque, int pin, int level)
{
    InputFixture *f = opaque;

    if (f->mode_level != level) {
        f->mode_changes++;
    }
    f->mode_level = level;
}

static void selector_level(void *opaque, int pin, int level)
{
    InputFixture *f = opaque;

    if (f->selector_level != level) {
        f->selector_changes++;
    }
    f->selector_level = level;
}

static InputFixture *input_new_full(bool expander)
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
    f->mode_level = -1;
    f->mode = qemu_allocate_irq(mode_level, f, 0);
    qdev_connect_gpio_out_named(f->input, "mode-adc", 0, f->mode);
    if (expander) {
        for (unsigned i = 0; i < ARRAY_SIZE(expander_pads); i++) {
            f->expander_level[i] = -1;
            f->expander[i] = qemu_allocate_irq(expander_level, f, i);
            qdev_connect_gpio_out_named(f->input, "expander-pad", i,
                                        f->expander[i]);
            g_assert_true(qdev_get_gpio_out_connector(f->input,
                "expander-pad", i) == f->expander[i]);
        }
    }
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
    /* Hotplug realization resets each GPIO and disconnects its inputs. Match
     * the board's receiver-before-provider order; the late receiver hold case
     * below separately checks publication after a subsequent reset.
     */
    for (unsigned i = 0; i < ARRAY_SIZE(f->gpio); i++) {
        g_assert_true(sysbus_realize(SYS_BUS_DEVICE(f->gpio[i]), &error_abort));
    }
    g_assert_true(qdev_realize(f->input, NULL, &error_abort));
    return f;
}

static InputFixture *input_new(void)
{
    return input_new_full(false);
}

static void input_free(InputFixture *f)
{
    qdev_unrealize(f->input);
    g_assert_cmpint(f->selector_level, ==, -1);
    g_assert_cmpint(f->mode_level, ==, -1);
    for (unsigned i = 0; i < ARRAY_SIZE(f->expander); i++) {
        if (f->expander[i]) {
            g_assert_cmpint(f->expander_level[i], ==, -1);
            qemu_free_irq(f->expander[i]);
        }
    }
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
    qemu_free_irq(f->mode);
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

static void expander_levels(InputFixture *f, uint32_t down)
{
    for (unsigned i = 0; i < ARRAY_SIZE(expander_pads); i++) {
        g_assert_cmpint(f->expander_level[i], ==,
                        !(down & (1U << expander_pads[i])));
    }
    levels(f, 0, "expander-does-not-drive-direct-GPIO");
    g_assert_cmpint(f->selector_level, ==, 1);
    g_assert_cmpint(f->mode_level, ==, 3747);
}

static void test_expander_contacts(void)
{
    for (unsigned cycle = 0; cycle < 2; cycle++) {
        InputFixture *f = input_new_full(true);
        unsigned changes[16];

        expander_levels(f, 0);
        for (unsigned i = 0; i < ARRAY_SIZE(expander_keys); i++) {
            uint32_t down = 1U << expander_pads[i];
            unsigned before = f->expander_changes[i];

            key(expander_keys[i], true);
            expander_levels(f, down);
            g_assert_cmpuint(f->expander_changes[i], ==, before + 1);
            key(expander_keys[i], true);
            expander_levels(f, down);
            g_assert_cmpuint(f->expander_changes[i], ==, before + 1);
            key(expander_keys[i], false);
            expander_levels(f, 0);
            g_assert_cmpuint(f->expander_changes[i], ==, before + 2);
            key(expander_keys[i], false);
            g_assert_cmpuint(f->expander_changes[i], ==, before + 2);
        }
        for (unsigned i = 0; i < ARRAY_SIZE(expander_keys); i++) {
            key(expander_keys[i], true);
            changes[i] = f->expander_changes[i];
        }
        expander_levels(f, 0xe0fbfcU);
        receiver_holds_after_frontend(f);
        board_reset();
        expander_levels(f, 0xe0fbfcU);
        key(KEY_ESC, true);
        key(KEY_ESC, false);
        for (unsigned i = 0; i < ARRAY_SIZE(expander_keys); i++) {
            key(expander_keys[i], true);
            g_assert_cmpuint(f->expander_changes[i], ==, changes[i]);
        }
        /* Unplug releases every contact, including a held-low one. */
        input_free(f);
        board_reset();
        for (unsigned i = 0; i < ARRAY_SIZE(expander_keys); i++) {
            key(expander_keys[i], false);
        }
    }
}

static void test_expander_directions(void)
{
    static const struct {
        unsigned first, second;
        uint32_t mask;
    } diagonal[] = {
        { KEY_RIGHT, KEY_UP, 0x60 }, { KEY_RIGHT, KEY_DOWN, 0x50 },
        { KEY_LEFT, KEY_UP, 0xa0 }, { KEY_LEFT, KEY_DOWN, 0x90 },
        { KEY_D, KEY_W, 0x6000 }, { KEY_D, KEY_S, 0x5000 },
        { KEY_A, KEY_W, 0xa000 }, { KEY_A, KEY_S, 0x9000 },
    };
    InputFixture *f = input_new_full(true);
    unsigned changes[16];

    for (unsigned i = 0; i < ARRAY_SIZE(diagonal); i++) {
        key(diagonal[i].first, true);
        key(diagonal[i].second, true);
        expander_levels(f, diagonal[i].mask);
        key(diagonal[i].first, false);
        key(diagonal[i].second, false);
        expander_levels(f, 0);
    }
    key(KEY_RIGHT, true);
    key(KEY_UP, true);
    key(KEY_A, true);
    key(KEY_S, true);
    key(KEY_F20, true);
    key(KEY_ENTER, true);
    key(KEY_F22, true);
    expander_levels(f, 0x9a68);
    memcpy(changes, f->expander_changes, sizeof(changes));
    board_reset();
    receiver_holds_after_frontend(f);
    expander_levels(f, 0x9a68);
    g_assert_cmpmem(changes, sizeof(changes), f->expander_changes,
                    sizeof(f->expander_changes));
    key(KEY_UP, false);
    expander_levels(f, 0x9a48);
    key(KEY_S, false);
    expander_levels(f, 0x8a48);
    key(KEY_ENTER, false);
    expander_levels(f, 0x8a40);
    key(KEY_RIGHT, false);
    key(KEY_A, false);
    key(KEY_F20, false);
    key(KEY_F22, false);
    expander_levels(f, 0);
    input_free(f);
}

static void advance(int64_t ns)
{
    cpus_set_virtual_clock(qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + ns);
    qemu_clock_run_timers(QEMU_CLOCK_VIRTUAL);
}

static void property_reject(InputFixture *f, const char *name, int64_t value)
{
    Error *error = NULL;

    g_assert_false(object_property_set_int(OBJECT(f->input), name, value,
                                          &error));
    g_assert_nonnull(error);
    error_free(error);
}

static void step(InputFixture *f, unsigned dial, int value)
{
    const char *name = dial ? "dial1-step" : "dial0-step";

    g_assert_true(object_property_set_int(OBJECT(f->input), name, value,
                                         &error_abort));
}

static unsigned contact(InputFixture *f, unsigned id)
{
    MemoryRegion *regs = sysbus_mmio_get_region(
        SYS_BUS_DEVICE(f->gpio[id >> 4]), 0);
    uint64_t idr;

    g_assert_cmpint(memory_region_dispatch_read(regs, 0x10, &idr, MO_32,
                        MEMTXATTRS_UNSPECIFIED), ==, MEMTX_OK);
    return (idr >> (id & 15)) & 1;
}

static void dial_state(InputFixture *f, unsigned dial, unsigned expected)
{
    unsigned first = dial ? 0x47 : 0x5b;
    unsigned second = dial ? 0x4d : 0x5e;

    g_assert_cmpuint((contact(f, first) << 1) | contact(f, second), ==,
                     expected);
}

static void test_mode(void)
{
    static const int counts[] = {
        115, 473, 975, 1506, 2057, 2604, 3136, 3747,
    };
    InputFixture *f = input_new();
    DeviceState *input;
    Error *error = NULL;
    unsigned changes;

    g_assert_cmpint(object_property_get_int(OBJECT(f->input), "mode-position",
                                            &error_abort), ==, 7);
    g_assert_cmpint(f->mode_level, ==, 3747);
    property_reject(f, "mode-position", -1);
    property_reject(f, "mode-position", 8);
    property_reject(f, "mode-position", INT64_MAX);
    g_assert_cmpint(f->mode_level, ==, 3747);
    for (unsigned i = 0; i < ARRAY_SIZE(counts); i++) {
        g_assert_true(object_property_set_int(OBJECT(f->input), "mode-position",
                                             i, &error_abort));
        g_assert_cmpint(f->mode_level, ==, counts[i]);
        changes = f->mode_changes;
        advance(10000000);
        receiver_holds_after_frontend(f);
        board_reset();
        g_assert_cmpint(f->mode_level, ==, counts[i]);
        g_assert_cmpuint(f->mode_changes, ==, changes);
        levels(f, 0, "mode-does-not-drive-digital-pads");
    }
    key(KEY_F14, true); /* Position7 wraps to0, repeat is not another turn. */
    key(KEY_F14, true);
    g_assert_cmpint(f->mode_level, ==, 115);
    board_reset();
    key(KEY_F14, true);
    g_assert_cmpint(f->mode_level, ==, 115);
    key(KEY_F14, false);
    key(KEY_F15, true);
    g_assert_cmpint(f->mode_level, ==, 3747);
    key(KEY_F15, true);
    key(KEY_F15, false);
    g_assert_cmpint(f->mode_level, ==, 3747);
    input_free(f);

    /* The maintained position can also be selected before realize. */
    input = qdev_new(TYPE_NIKON_INPUT);
    g_assert_true(object_property_set_int(OBJECT(input), "mode-position", 2,
                                         &error_abort));
    g_assert_cmpint(object_property_get_int(OBJECT(input), "mode-position",
                                            &error_abort), ==, 2);
    g_assert_false(object_property_set_int(OBJECT(input), "dial0-step", 1,
                                          &error));
    g_assert_nonnull(error);
    error_free(error);
    object_unref(OBJECT(input));
}

static void test_dial_phases(void)
{
    static const unsigned forward[] = { 2, 0, 1, 3 };
    static const unsigned reverse[] = { 1, 0, 2, 3 };
    InputFixture *f = input_new();

    for (unsigned dial = 0; dial < 2; dial++) {
        property_reject(f, dial ? "dial1-step" : "dial0-step", 2);
        property_reject(f, dial ? "dial1-step" : "dial0-step", -2);
        step(f, dial, 1);
        dial_state(f, dial, 3);
        for (unsigned phase = 0; phase < 4; phase++) {
            advance(49999999);
            dial_state(f, dial, phase ? forward[phase - 1] : 3);
            advance(1);
            dial_state(f, dial, forward[phase]);
            dial_state(f, !dial, 3);
        }
        step(f, dial, -1);
        for (unsigned phase = 0; phase < 4; phase++) {
            advance(50000000);
            dial_state(f, dial, reverse[phase]);
        }
    }
    /* Independent simultaneous directions and preserved active deadline. */
    step(f, 0, 1);
    step(f, 1, -1);
    advance(40000000);
    step(f, 0, -1);
    advance(10000000);
    dial_state(f, 0, 2);
    dial_state(f, 1, 1);
    for (unsigned i = 1; i < 4; i++) {
        advance(50000000);
        dial_state(f, 0, forward[i]);
        dial_state(f, 1, reverse[i]);
    }
    for (unsigned i = 0; i < 4; i++) {
        advance(50000000);
        dial_state(f, 0, reverse[i]);
        dial_state(f, 1, 3);
    }
    input_free(f);
}

static void test_dial_queue(void)
{
    static const unsigned forward[] = { 2, 0, 1, 3 };
    InputFixture *f = input_new();

    for (unsigned dial = 0; dial < 2; dial++) {
        for (unsigned i = 0; i < 8; i++) {
            step(f, dial, 1);
        }
        property_reject(f, dial ? "dial1-step" : "dial0-step", -1);
        dial_state(f, dial, 3);
        for (unsigned i = 0; i < 32; i++) {
            advance(50000000);
            dial_state(f, dial, forward[i % 4]);
        }
        advance(50000000);
        dial_state(f, dial, 3);
        /* Queue slots are reusable after the full sequence, including wrap. */
        step(f, dial, -1);
        advance(50000000);
        dial_state(f, dial, 1);
        step(f, dial, 0);
        advance(200000000);
        dial_state(f, dial, 3);
    }
    input_free(f);
}

static void test_dial_ownership(void)
{
    InputFixture *f = input_new();

    step(f, 0, 1);
    step(f, 0, -1);
    step(f, 1, -1);
    advance(50000000);
    key(KEY_F8, true); /* Cancel dial0 and yield both contacts to raw keys. */
    dial_state(f, 0, 1);
    property_reject(f, "dial0-step", 1);
    advance(50000000);
    dial_state(f, 0, 1);
    dial_state(f, 1, 0);
    key(KEY_F9, true);
    dial_state(f, 0, 0);
    step(f, 0, 0); /* Explicit cancel preserves the raw owner. */
    dial_state(f, 0, 0);
    receiver_holds_after_frontend(f);
    board_reset();
    dial_state(f, 0, 0);
    key(KEY_F8, false);
    key(KEY_F9, false);
    dial_state(f, 0, 3);
    step(f, 0, 1);
    board_reset();
    advance(500000000);
    dial_state(f, 0, 3);
    dial_state(f, 1, 3);
    for (unsigned i = 0; i < 4; i++) {
        unsigned code = KEY_F16 + i;
        unsigned dial = i / 2;

        key(code, true);
        key(code, true);
        advance(50000000);
        dial_state(f, dial, i & 1 ? 1 : 2);
        step(f, dial, 0);
        advance(250000000);
        dial_state(f, dial, 3); /* A repeated key did not queue another turn. */
        board_reset();
        key(code, true);
        advance(250000000);
        dial_state(f, dial, 3);
        key(code, false);
    }
    step(f, 0, 1);
    step(f, 1, -1);
    input_free(f);
    advance(500000000); /* Unrealize cancelled both timers. */
    board_reset();
}

static void test_shutter_af_pads(void)
{
    InputFixture *f = input_new();
    bool val;

    /* Verify defaults are released (false, active-low pads high). */
    val = object_property_get_bool(OBJECT(f->input), "half-press",
                                   &error_abort);
    g_assert_false(val);
    val = object_property_get_bool(OBJECT(f->input), "shutter-half-press",
                                   &error_abort);
    g_assert_false(val);
    val = object_property_get_bool(OBJECT(f->input), "full-press",
                                   &error_abort);
    g_assert_false(val);
    val = object_property_get_bool(OBJECT(f->input), "shutter-full-press",
                                   &error_abort);
    g_assert_false(val);
    val = object_property_get_bool(OBJECT(f->input), "af-on",
                                   &error_abort);
    g_assert_false(val);
    val = object_property_get_bool(OBJECT(f->input), "main-power",
                                   &error_abort);
    g_assert_false(val);
    levels(f, 0, "default-released");

    /* Test half-press property and pad 9 (PG1, KEY_F10). */
    object_property_set_bool(OBJECT(f->input), "half-press", true,
                             &error_abort);
    val = object_property_get_bool(OBJECT(f->input), "half-press",
                                   &error_abort);
    g_assert_true(val);
    val = object_property_get_bool(OBJECT(f->input), "shutter-half-press",
                                   &error_abort);
    g_assert_true(val);
    levels(f, 1U << NIKON_INPUT_PAD_HALF_PRESS, "half-press-down");

    object_property_set_bool(OBJECT(f->input), "half-press", false,
                             &error_abort);
    levels(f, 0, "half-press-up");

    /* Test af-on property and pad 10 (PG2, KEY_F11). */
    object_property_set_bool(OBJECT(f->input), "af-on", true, &error_abort);
    val = object_property_get_bool(OBJECT(f->input), "af-on", &error_abort);
    g_assert_true(val);
    levels(f, 1U << NIKON_INPUT_PAD_AF_ON, "af-on-down");

    object_property_set_bool(OBJECT(f->input), "af-on", false, &error_abort);
    levels(f, 0, "af-on-up");

    /* Test full-press property and pad 11 (PG13, KEY_F12). */
    object_property_set_bool(OBJECT(f->input), "full-press", true,
                             &error_abort);
    levels(f, 1U << NIKON_INPUT_PAD_FULL_PRESS, "full-press-down");
    object_property_set_bool(OBJECT(f->input), "full-press", false,
                             &error_abort);
    levels(f, 0, "full-press-up");

    /* Test main-power property and pad 3 (PC5, KEY_F4). */
    object_property_set_bool(OBJECT(f->input), "main-power", true,
                             &error_abort);
    levels(f, 1U << NIKON_INPUT_PAD_MAIN_POWER, "main-power-down");
    object_property_set_bool(OBJECT(f->input), "main-power", false,
                             &error_abort);
    levels(f, 0, "main-power-up");

    /* Test keyboard key events for S1 (F10), AF-ON (F11), S2 (F12). */
    key(KEY_F10, true);
    val = object_property_get_bool(OBJECT(f->input), "half-press",
                                   &error_abort);
    g_assert_true(val);
    levels(f, 1U << NIKON_INPUT_PAD_HALF_PRESS, "key-f10-half-press");

    key(KEY_F11, true);
    val = object_property_get_bool(OBJECT(f->input), "af-on", &error_abort);
    g_assert_true(val);
    levels(f, (1U << NIKON_INPUT_PAD_HALF_PRESS) |
              (1U << NIKON_INPUT_PAD_AF_ON),
           "key-f10-f11-down");

    /* Reset retains held state. */
    board_reset();
    levels(f, (1U << NIKON_INPUT_PAD_HALF_PRESS) |
              (1U << NIKON_INPUT_PAD_AF_ON),
           "held-after-reset");

    key(KEY_F10, false);
    key(KEY_F11, false);
    levels(f, 0, "keys-released");

    input_free(f);
    board_reset();
}

static void test_menu_navigation_properties(void)
{
    static const struct {
        const char *name;
        unsigned exp_idx;
        const char *alias;
    } buttons[] = {
        { "menu", NIKON_INPUT_EXP_MENU, "menu-button" },
        { "ok", NIKON_INPUT_EXP_OK, "ok-button" },
        { "playback", NIKON_INPUT_EXP_PLAYBACK, NULL },
        { "delete", NIKON_INPUT_EXP_DELETE, NULL },
        { "i-button", NIKON_INPUT_EXP_I, "i" },
        { "right", NIKON_INPUT_EXP_RIGHT, NULL },
        { "left", NIKON_INPUT_EXP_LEFT, NULL },
        { "up", NIKON_INPUT_EXP_UP, NULL },
        { "down", NIKON_INPUT_EXP_DOWN, NULL },
        { "zoom", NIKON_INPUT_EXP_ZOOM, NULL },
        { "thumbnail", NIKON_INPUT_EXP_THUMBNAIL, NULL },
        { "second-right", NIKON_INPUT_EXP_SECOND_RIGHT, NULL },
        { "second-left", NIKON_INPUT_EXP_SECOND_LEFT, NULL },
        { "second-up", NIKON_INPUT_EXP_SECOND_UP, NULL },
        { "second-down", NIKON_INPUT_EXP_SECOND_DOWN, NULL },
        { "second-center", NIKON_INPUT_EXP_SECOND_CENTER, NULL },
    };
    InputFixture *f = input_new_full(true);
    Error *err = NULL;
    bool val;

    /* Verify initial state: all buttons released (high = 1) */
    for (unsigned i = 0; i < ARRAY_SIZE(buttons); i++) {
        val = object_property_get_bool(OBJECT(f->input), buttons[i].name,
                                       &error_abort);
        g_assert_false(val);
        g_assert_cmpint(f->expander_level[buttons[i].exp_idx], ==, 1);
        if (buttons[i].alias) {
            val = object_property_get_bool(OBJECT(f->input), buttons[i].alias,
                                           &error_abort);
            g_assert_false(val);
        }
    }

    /* Test press and release for every expander property */
    for (unsigned i = 0; i < ARRAY_SIZE(buttons); i++) {
        unsigned before_changes = f->expander_changes[buttons[i].exp_idx];

        /* Press */
        object_property_set_bool(OBJECT(f->input), buttons[i].name, true,
                                 &error_abort);
        val = object_property_get_bool(OBJECT(f->input), buttons[i].name,
                                       &error_abort);
        g_assert_true(val);
        g_assert_cmpint(f->expander_level[buttons[i].exp_idx], ==, 0);
        g_assert_cmpuint(f->expander_changes[buttons[i].exp_idx], ==,
                         before_changes + 1);

        /* Idempotent press (no duplicate level change) */
        object_property_set_bool(OBJECT(f->input), buttons[i].name, true,
                                 &error_abort);
        g_assert_cmpuint(f->expander_changes[buttons[i].exp_idx], ==,
                         before_changes + 1);

        /* Release */
        object_property_set_bool(OBJECT(f->input), buttons[i].name, false,
                                 &error_abort);
        val = object_property_get_bool(OBJECT(f->input), buttons[i].name,
                                       &error_abort);
        g_assert_false(val);
        g_assert_cmpint(f->expander_level[buttons[i].exp_idx], ==, 1);
        g_assert_cmpuint(f->expander_changes[buttons[i].exp_idx], ==,
                         before_changes + 2);

        /* Test alias if present */
        if (buttons[i].alias) {
            object_property_set_bool(OBJECT(f->input), buttons[i].alias, true,
                                     &error_abort);
            val = object_property_get_bool(OBJECT(f->input), buttons[i].name,
                                           &error_abort);
            g_assert_true(val);
            g_assert_cmpint(f->expander_level[buttons[i].exp_idx], ==, 0);

            object_property_set_bool(OBJECT(f->input), buttons[i].alias, false,
                                     &error_abort);
            val = object_property_get_bool(OBJECT(f->input), buttons[i].alias,
                                           &error_abort);
            g_assert_false(val);
            g_assert_cmpint(f->expander_level[buttons[i].exp_idx], ==, 1);
        }
    }

    /*
     * Simulate menu category navigation sequence:
     * 1. Open Menu: press then release "menu"
     * 2. Navigate through 6 categories down: press/release "down" x 6
     * 3. Enter category item list: press/release "right"
     * 4. Return to category list: press/release "left"
     * 5. Navigate 6 categories back up: press/release "up" x 6
     * 6. Enter category via "ok": press/release "ok"
     */
    const char *seq[] = {
        "menu", "down", "down", "down", "down", "down", "down",
        "right", "left",
        "up", "up", "up", "up", "up", "up",
        "ok"
    };
    for (unsigned s = 0; s < ARRAY_SIZE(seq); s++) {
        object_property_set_bool(OBJECT(f->input), seq[s], true, &error_abort);
        val = object_property_get_bool(OBJECT(f->input), seq[s], &error_abort);
        g_assert_true(val);
        object_property_set_bool(OBJECT(f->input), seq[s], false, &error_abort);
        val = object_property_get_bool(OBJECT(f->input), seq[s], &error_abort);
        g_assert_false(val);
    }

    /* Test reset retention on expander properties */
    object_property_set_bool(OBJECT(f->input), "menu", true, &error_abort);
    object_property_set_bool(OBJECT(f->input), "down", true, &error_abort);
    g_assert_cmpint(f->expander_level[NIKON_INPUT_EXP_MENU], ==, 0);
    g_assert_cmpint(f->expander_level[NIKON_INPUT_EXP_DOWN], ==, 0);

    board_reset();
    g_assert_cmpint(f->expander_level[NIKON_INPUT_EXP_MENU], ==, 0);
    g_assert_cmpint(f->expander_level[NIKON_INPUT_EXP_DOWN], ==, 0);

    object_property_set_bool(OBJECT(f->input), "menu", false, &error_abort);
    object_property_set_bool(OBJECT(f->input), "down", false, &error_abort);
    g_assert_cmpint(f->expander_level[NIKON_INPUT_EXP_MENU], ==, 1);
    g_assert_cmpint(f->expander_level[NIKON_INPUT_EXP_DOWN], ==, 1);

    /* Negative test: setting invalid type */
    g_assert_false(object_property_set_int(OBJECT(f->input), "menu", 42,
                                           &err));
    g_assert_nonnull(err);
    error_free(err);
    err = NULL;

    input_free(f);
    board_reset();
}

int main(int argc, char **argv)
{
    g_autofree char *directory = g_dir_make_tmp("nikonXXXXXX", NULL);
    g_autofree char *socket = g_build_filename(directory, "qtest", NULL);
    g_autofree char *chardev = g_strdup_printf("unix:%s,server=on,wait=off",
                                             socket);
    char *qemu_argv[] = {
        argv[0], (char *)"-machine", (char *)"none", (char *)"-accel",
        (char *)"qtest", (char *)"-display", (char *)"none",
        (char *)"-nodefaults", (char *)"-qtest", chardev, NULL
    };
    int result;

    g_assert_nonnull(directory);
    g_test_init(&argc, &argv, NULL);
    qemu_init(G_N_ELEMENTS(qemu_argv) - 1, qemu_argv);
    vm_start();
    g_test_add_func("/nikon/input/receivers-reset-lifecycle", test_receivers);
    g_test_add_func("/nikon/input/capture-selector", test_selector);
    g_test_add_func("/nikon/input/expander-contacts", test_expander_contacts);
    g_test_add_func("/nikon/input/expander-directions", test_expander_directions);
    g_test_add_func("/nikon/input/mode-detents", test_mode);
    g_test_add_func("/nikon/input/dial-phases", test_dial_phases);
    g_test_add_func("/nikon/input/dial-queue", test_dial_queue);
    g_test_add_func("/nikon/input/dial-ownership", test_dial_ownership);
    g_test_add_func("/nikon/input/shutter-af-pads", test_shutter_af_pads);
    g_test_add_func("/nikon/input/menu-navigation-properties",
                    test_menu_navigation_properties);
    result = g_test_run();
    qemu_cleanup(result);
    unlink(socket);
    rmdir(directory);
    return result;
}

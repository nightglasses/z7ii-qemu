/*
 * Explicit virtual lens packets over the native SSI word interface.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/bswap.h"
#include "qemu/timer.h"
#include "hw/core/sysbus.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-properties.h"
#include "hw/ssi/ssi.h"
#include "hw/nikon/devices.h"
#include "hw/nikon/peer.h"
#include "system/cpu-timers.h"
#include "system/runstate.h"
#include "system/system.h"

int (*qemu_main)(void);

typedef struct LensFixture {
    DeviceState *spi, *lens, *stream;
    SSIBus *bus;
    qemu_irq notify;
    unsigned falling;
    bool level;
} LensFixture;

static void notify(void *opaque, int index, int level)
{
    LensFixture *f = opaque;

    f->falling += f->level && !level;
    f->level = level;
}

static LensFixture *fixture(void)
{
    LensFixture *f = g_new0(LensFixture, 1);

    f->spi = qdev_new(TYPE_NIKON_SPI);
    object_property_add_child(object_get_objects_root(), "spi[*]",
                              OBJECT(f->spi));
    g_assert_true(sysbus_realize(SYS_BUS_DEVICE(f->spi), &error_abort));
    f->bus = (SSIBus *)qdev_get_child_bus(f->spi, "spi");
    f->lens = qdev_new("nikon-lens-functional");
    f->stream = qdev_new(TYPE_NIKON_SPI);
    object_property_add_child(object_get_objects_root(), "stream[*]",
                              OBJECT(f->stream));
    g_assert_true(sysbus_realize(SYS_BUS_DEVICE(f->stream), &error_abort));
    device_cold_reset(f->stream);
    object_property_set_link(OBJECT(f->lens), "stream", OBJECT(f->stream),
                              &error_abort);
    object_property_add_child(object_get_objects_root(), "lens[*]",
                              OBJECT(f->lens));
    g_assert_true(qdev_realize(f->lens, BUS(f->bus), &error_abort));
    f->notify = qemu_allocate_irq(notify, f, 0);
    qdev_connect_gpio_out_named(f->lens, "notify", 0, f->notify);
    device_cold_reset(f->lens);
    return f;
}

static void cleanup(LensFixture *f)
{
    qdev_unrealize(f->lens);
    object_unparent(OBJECT(f->lens));
    object_unref(OBJECT(f->lens));
    qdev_unrealize(f->stream);
    object_unparent(OBJECT(f->stream));
    object_unref(OBJECT(f->stream));
    qemu_free_irq(f->notify);
    qdev_unrealize(f->spi);
    object_unparent(OBJECT(f->spi));
    object_unref(OBJECT(f->spi));
    g_free(f);
}

static void advance(int64_t ns)
{
    cpus_set_virtual_clock(qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + ns);
    qemu_clock_run_timers(QEMU_CLOCK_VIRTUAL);
}

static void checksum(uint8_t *bytes, unsigned size)
{
    unsigned sum = 0;

    for (unsigned i = 0; i < size - 1; i++) {
        sum += bytes[i];
    }
    bytes[size - 1] = -sum;
}

static void exchange(LensFixture *f, uint8_t *bytes, unsigned size)
{
    for (unsigned i = 0; i < size; i += 2) {
        uint16_t reply = ssi_transfer(f->bus, lduw_le_p(bytes + i));

        stw_le_p(bytes + i, reply);
    }
}

static void packet(LensFixture *f, uint8_t command, uint8_t subcommand,
                   uint8_t *payload, unsigned size)
{
    uint8_t header[] = { 0x55, 1, 1, command, subcommand, 0, 0, 0 };
    unsigned sum = 0;

    if (command == 0x1f) {
        header[2] = 0xff;
    }
    stw_le_p(header + 5, (size - 2) / 2);
    checksum(header, sizeof(header));
    exchange(f, header, sizeof(header));
    g_assert_true(f->level);
    advance(1000000);
    g_assert_false(f->level);
    payload[0] = 0x55;
    checksum(payload, size);
    exchange(f, payload, size);
    g_assert_true(f->level);
    advance(1000000);
    g_assert_false(f->level);
    for (unsigned i = 0; i < size; i++) {
        sum += payload[i];
    }
    g_assert_cmphex(payload[0], ==, 0x55);
    g_assert_cmphex(sum & 0xff, ==, 0);
}

static void test_initialization(void)
{
    static const struct {
        uint8_t command, subcommand;
        unsigned size;
    } requests[] = {
        { 0x1f, 0, 4 },
        { 0x10, 0, 28 }, { 0x11, 0, 74 }, { 0x12, 0, 66 },
        { 0x13, 0, 14 }, { 0x24, 0, 7426 }, { 0x24, 0x10, 3468 },
        { 0x15, 0, 6 }, { 0x21, 0, 6 }, { 0x20, 0, 258 },
        { 0x20, 1, 24 }, { 0x27, 0, 6 }, { 0x16, 0, 4 },
    };
    LensFixture *f = fixture();
    uint8_t bytes[7426];

    for (unsigned i = 0; i < ARRAY_SIZE(requests); i++) {
        memset(bytes, 0, sizeof(bytes));
        if (requests[i].command == 0x1f) {
            bytes[1] = 1;
        }
        packet(f, requests[i].command, requests[i].subcommand,
               bytes, requests[i].size);
        if (requests[i].command == 0x13) {
            advance(2000000);
        } else if (requests[i].command == 0x10) {
            g_assert_cmphex(lduw_le_p(bytes + 1), ==, 0x7ffe);
            /* Both original normal-family initialization conditions hold. */
            g_assert_cmphex(bytes[12], ==, 3);
        } else if (requests[i].command == 0x12) {
            g_assert_cmpuint(lduw_le_p(bytes + 1), ==, 960);
            g_assert_cmpuint(lduw_le_p(bytes + 3), ==, 960);
            g_assert_cmpuint(lduw_le_p(bytes + 7), ==, 50);
            g_assert_cmpuint(lduw_le_p(bytes + 11), ==, 50);
            g_assert_cmpuint(bytes[25], ==, 1);
        } else if (requests[i].command == 0x20 && !requests[i].subcommand) {
            g_assert_cmphex(bytes[169] & 0x60, ==, 0x40);
            g_assert_cmphex(bytes[216], ==, 3);
        } else if (requests[i].command == 0x20) {
            g_assert_cmpuint(lduw_le_p(bytes + 7), ==, 50);
        }
    }
    g_assert_cmpuint(f->falling, ==, 2 * ARRAY_SIZE(requests));
    cleanup(f);
}

static void test_rejected_header(void)
{
    LensFixture *f = fixture();
    uint8_t header[] = { 0x55, 0, 1, 0x10, 0, 13, 0, 0 };

    /* Bad checksum must produce no completion edge or successful response. */
    exchange(f, header, sizeof(header));
    advance(10000000);
    g_assert_true(f->level);
    g_assert_cmpuint(f->falling, ==, 0);
    device_cold_reset(f->lens);
    g_assert_false(f->level);
    cleanup(f);
}

static void test_operating_state(void)
{
    LensFixture *f = fixture();
    uint8_t bytes[258] = { 0 };

    packet(f, 0x20, 0, bytes, sizeof(bytes));
    g_assert_cmphex(bytes[216], ==, 3);
    g_assert_cmphex(bytes[169], ==, 0x20);
    memset(bytes, 0, sizeof(bytes));
    packet(f, 0x13, 0, bytes, 14);
    /* This reply is snapshotted before the two-millisecond commit. */
    memset(bytes, 0, sizeof(bytes));
    packet(f, 0x20, 0, bytes, sizeof(bytes));
    g_assert_cmphex(bytes[216], ==, 3);
    g_assert_cmphex(bytes[169], ==, 0x20);
    g_assert_cmphex(bytes[203], ==, 8);
    memset(bytes, 0, sizeof(bytes));
    packet(f, 0x20, 0, bytes, sizeof(bytes));
    /* Original getter12404 and admission40000500 require logical state3. */
    g_assert_cmphex(bytes[216], ==, 3);
    g_assert_cmphex(bytes[169], ==, 0x40);
    g_assert_cmphex(bytes[203], ==, 0);
    g_assert_cmpuint(lduw_le_p(bytes + 1), ==, 960);
    device_cold_reset(f->lens);
    advance(10000000);
    memset(bytes, 0, sizeof(bytes));
    packet(f, 0x20, 0, bytes, sizeof(bytes));
    g_assert_cmphex(bytes[216], ==, 3);
    g_assert_cmphex(bytes[169], ==, 0x20);
    cleanup(f);
}

static void test_reset_partial(void)
{
    LensFixture *f = fixture();
    uint8_t bytes[28] = { 0 };

    ssi_transfer(f->bus, 0x0055);
    g_assert_true(f->level);
    device_cold_reset(f->lens);
    advance(10000000);
    g_assert_false(f->level);
    packet(f, 0x10, 0, bytes, sizeof(bytes));
    cleanup(f);
}

static void test_phase_deadline(void)
{
    LensFixture *f = fixture();
    uint8_t header[] = { 0x55, 0, 0xff, 0x1f, 0, 1, 0, 0 };
    uint8_t payload[] = { 0x55, 1, 0, 0 };

    checksum(header, sizeof(header));
    exchange(f, header, sizeof(header));
    advance(999999);
    g_assert_true(f->level);
    advance(1);
    g_assert_false(f->level);
    checksum(payload, sizeof(payload));
    exchange(f, payload, sizeof(payload));
    advance(999999);
    g_assert_true(f->level);
    advance(1);
    g_assert_false(f->level);
    g_assert_cmpuint(f->falling, ==, 2);
    cleanup(f);
}

static uint64_t count(LensFixture *f, const char *name)
{
    return object_property_get_uint(OBJECT(f->lens), name, &error_abort);
}

static void test_aperture_idle(void)
{
    LensFixture *f = fixture();
    uint8_t bytes[258] = { 0 };

    packet(f, 0x13, 0, bytes, 14);
    advance(2000000);
    for (unsigned sub = 0; sub < 8; sub++) {
        memset(bytes, 0, sizeof(bytes));
        packet(f, 0xa0, sub, bytes, 24);
    }
    memset(bytes, 0, sizeof(bytes));
    packet(f, 0xa1, 0, bytes, 10);
    for (unsigned sub = 0; sub < 2; sub++) {
        memset(bytes, 0, sizeof(bytes));
        packet(f, 0xa3, sub, bytes, 4);
    }
    g_assert_cmpuint(count(f, "aperture-noops"), ==, 11);
    memset(bytes, 0, sizeof(bytes));
    packet(f, 0x20, 0, bytes, 258);
    for (unsigned i = 0; i < 5; i++) {
        const unsigned aperture[] = { 1, 9, 15, 162, 164 };

        g_assert_cmpuint(lduw_le_p(bytes + aperture[i]), ==, 960);
    }
    g_assert_cmpuint(lduw_le_p(bytes + 197), ==, 0);
    g_assert_cmpuint(lduw_le_p(bytes + 199), ==, 1152);
    g_assert_cmpuint(lduw_le_p(bytes + 201), ==, 0);
    g_assert_cmpuint(lduw_le_p(bytes + 204), ==, 0);
    g_assert_cmphex(bytes[203] & 0x18, ==, 0);
    memset(bytes, 0, sizeof(bytes));
    packet(f, 0x20, 1, bytes, 24);
    g_assert_cmpuint(lduw_le_p(bytes + 1), ==, 960);
    g_assert_cmpuint(lduw_le_p(bytes + 3), ==, 960);
    g_assert_cmpuint(count(f, "configuration-generation"), ==, 1);
    device_cold_reset(f->lens);
    g_assert_cmpuint(count(f, "aperture-noops"), ==, 0);
    cleanup(f);
}

static void test_aperture_rejection(void)
{
    static const struct {
        uint8_t command;
        unsigned size, target, parameter, extra, tail;
    } cases[] = {
        { 0xa0, 24, 1153, 0, 0, 0 }, { 0xa0, 24, 65535, 0, 0, 0 },
        { 0xa0, 24, 192, 0, 0, 3 }, { 0xa0, 24, 0, 0, 0, 22 },
        { 0xa1, 10, 192, 0, 0, 0 }, { 0xa1, 10, 192, 1, 0, 0 },
        { 0xa1, 10, 192, 65535, 1, 0 },
        { 0xa1, 10, 192, 65535, 0x80000000, 0 },
        { 0xa1, 10, 1153, 65535, 0, 0 },
        { 0xa3, 4, 1153, 0, 0, 0 },
    };
    LensFixture *f = fixture();
    uint8_t bytes[24], header[8];

    for (unsigned i = 0; i < ARRAY_SIZE(cases); i++) {
        unsigned falling;

        device_cold_reset(f->lens);
        memset(bytes, 0, sizeof(bytes));
        packet(f, 0x13, 0, bytes, 14);
        advance(2000000);
        memset(header, 0, sizeof(header));
        header[0] = 0x55;
        header[2] = 1;
        header[3] = cases[i].command;
        stw_le_p(header + 5, (cases[i].size - 2) / 2);
        checksum(header, sizeof(header));
        exchange(f, header, sizeof(header));
        advance(1000000);
        g_assert_false(f->level);
        falling = f->falling;
        memset(bytes, 0, sizeof(bytes));
        bytes[0] = 0x55;
        stw_le_p(bytes + 1, cases[i].target);
        if (cases[i].command == 0xa1) {
            stw_le_p(bytes + 3, cases[i].parameter);
            stl_le_p(bytes + 5, cases[i].extra);
        }
        if (cases[i].tail) {
            bytes[cases[i].tail] = 1;
        }
        checksum(bytes, cases[i].size);
        exchange(f, bytes, cases[i].size);
        advance(10000000);
        g_assert_true(f->level);
        g_assert_cmpuint(f->falling, ==, falling);
        g_assert_cmpuint(count(f, "completed"), ==, 1);
        g_assert_cmpuint(count(f, "rejected"), ==, 1);
        g_assert_cmpuint(count(f, "aperture-noops"), ==, 0);
        g_assert_cmpuint(count(f, "aperture-generation"), ==, 0);
    }
    cleanup(f);
}

static void aperture_request(LensFixture *f, uint8_t command, uint8_t sub,
                             unsigned target)
{
    uint8_t bytes[24] = { 0 };
    unsigned size = command == 0xa0 ? 24 : command == 0xa1 ? 10 : 4;

    stw_le_p(bytes + 1, target);
    if (command == 0xa1) {
        stw_le_p(bytes + 3, 65535);
    }
    packet(f, command, sub, bytes, size);
}

static void aperture_status(LensFixture *f, unsigned position, bool busy)
{
    uint8_t bytes[258] = { 0 };
    const unsigned fixed[] = { 1, 15, 162, 164 };

    packet(f, 0x20, 0, bytes, sizeof(bytes));
    g_assert_cmpuint(lduw_le_p(bytes + 9), ==, 960 + position);
    g_assert_cmpuint(lduw_le_p(bytes + 197), ==, position);
    g_assert_cmpuint(lduw_le_p(bytes + 199), ==, 1152);
    g_assert_cmphex(bytes[203] & 0x18, ==, busy ? 0x18 : 0);
    for (unsigned i = 0; i < ARRAY_SIZE(fixed); i++) {
        g_assert_cmpuint(lduw_le_p(bytes + fixed[i]), ==, 960);
    }
    g_assert_cmpuint(lduw_le_p(bytes + 201), ==, 0);
    g_assert_cmpuint(lduw_le_p(bytes + 204), ==, 0);
    g_assert_cmphex(bytes[216], ==, 3);
}

static void test_aperture_motion(void)
{
    LensFixture *f = fixture();
    uint8_t bytes[66] = { 0 };
    unsigned falling;

    packet(f, 0x13, 0, bytes, 14);
    advance(2000000);
    aperture_request(f, 0xa0, 0, 1152);
    /* Packet ACK is complete after 1 ms; six stops take 120 ms. */
    g_assert_cmpuint(count(f, "aperture-generation"), ==, 0);
    aperture_status(f, 9, true);
    advance(57000000);
    aperture_status(f, 576, true);
    falling = f->falling;
    advance(57999999);
    g_assert_cmpuint(count(f, "aperture-generation"), ==, 0);
    advance(1);
    g_assert_cmpuint(count(f, "aperture-generation"), ==, 1);
    g_assert_cmpuint(f->falling, ==, falling);
    aperture_status(f, 1152, false);

    /* Reopening uses the same travel coordinate, not an incremental delta. */
    aperture_request(f, 0xa3, 1, 576);
    advance(59000000);
    aperture_status(f, 576, false);
    aperture_request(f, 0xa1, 0, 192);
    advance(39000000);
    aperture_status(f, 192, false);
    aperture_request(f, 0xa3, 0, 0);
    advance(19000000);
    aperture_status(f, 0, false);
    g_assert_cmpuint(count(f, "aperture-generation"), ==, 4);

    /* A one-unit move still has its independent two-millisecond deadline. */
    aperture_request(f, 0xa0, 0, 1);
    advance(999999);
    g_assert_cmpuint(count(f, "aperture-generation"), ==, 4);
    advance(1);
    g_assert_cmpuint(count(f, "aperture-generation"), ==, 5);
    aperture_status(f, 1, false);

    /* Maximum aperture at both focal endpoints is independent of travel. */
    packet(f, 0x12, 0, bytes, sizeof(bytes));
    g_assert_cmpuint(lduw_le_p(bytes + 1), ==, 960);
    g_assert_cmpuint(lduw_le_p(bytes + 3), ==, 960);
    cleanup(f);
}

static void test_aperture_encodings(void)
{
    LensFixture *f = fixture();
    uint8_t bytes[14] = { 0 };
    unsigned generation = 0;

    packet(f, 0x13, 0, bytes, sizeof(bytes));
    advance(2000000);
    for (unsigned sub = 0; sub < 8; sub++) {
        unsigned target = sub & 1 ? 0 : 192;

        aperture_request(f, 0xa0, sub, target);
        advance(19000000);
        aperture_status(f, target, false);
        g_assert_cmpuint(count(f, "aperture-generation"), ==, ++generation);
    }
    for (unsigned sub = 0; sub < 2; sub++) {
        unsigned target = sub ? 0 : 192;

        aperture_request(f, 0xa3, sub, target);
        advance(19000000);
        aperture_status(f, target, false);
        g_assert_cmpuint(count(f, "aperture-generation"), ==, ++generation);
    }
    aperture_request(f, 0xa1, 0, 192);
    advance(19000000);
    aperture_status(f, 192, false);
    g_assert_cmpuint(count(f, "aperture-generation"), ==, ++generation);
    aperture_request(f, 0xa0, 7, 192);
    g_assert_cmpuint(count(f, "aperture-generation"), ==, generation);
    g_assert_cmpuint(count(f, "aperture-noops"), ==, 1);
    cleanup(f);
}

static void test_aperture_lifecycle(void)
{
    LensFixture *f = fixture();
    uint8_t bytes[24] = { 0 };
    uint8_t header[] = { 0x55, 0, 1, 0xa0, 0, 11, 0, 0 };
    g_autofree char *runtime = NULL;
    unsigned falling;

    packet(f, 0x13, 0, bytes, 14);
    advance(2000000);
    aperture_request(f, 0xa0, 0, 1152);
    checksum(header, sizeof(header));
    exchange(f, header, sizeof(header));
    advance(1000000);
    falling = f->falling;
    memset(bytes, 0, sizeof(bytes));
    bytes[0] = 0x55;
    stw_le_p(bytes + 1, 192);
    checksum(bytes, sizeof(bytes));
    exchange(f, bytes, sizeof(bytes));
    advance(1000000);
    g_assert_true(f->level);
    g_assert_cmpuint(f->falling, ==, falling);
    runtime = object_property_get_str(OBJECT(f->lens), "runtime", &error_abort);
    g_assert_nonnull(strstr(runtime, "target=1152 aperture-pending=1"));

    device_cold_reset(f->lens);
    advance(200000000);
    g_assert_cmpuint(count(f, "aperture-generation"), ==, 0);
    aperture_status(f, 0, false);
    memset(bytes, 0, sizeof(bytes));
    packet(f, 0x13, 0, bytes, 14);
    advance(2000000);
    aperture_request(f, 0xa3, 0, 1152);
    advance(59000000);
    qdev_unrealize(f->lens);
    advance(200000000);
    g_clear_pointer(&runtime, g_free);
    runtime = object_property_get_str(OBJECT(f->lens), "runtime", &error_abort);
    g_assert_nonnull(strstr(runtime,
        "aperture=576 target=576 aperture-pending=0 aperture-generation=0"));
    cleanup(f);
}

static void test_state_and_sync(void)
{
    LensFixture *f = fixture();
    uint8_t bytes[14] = { 0 };

    packet(f, 0x13, 0, bytes, 14);
    g_assert_cmpuint(count(f, "configuration-generation"), ==, 0);
    memset(bytes, 0, sizeof(bytes));
    packet(f, 0x15, 0, bytes, 6);
    g_assert_cmphex(bytes[1], ==, 1);
    g_assert_cmphex(bytes[2], ==, 1);
    g_assert_cmpuint(count(f, "configuration-generation"), ==, 1);
    advance(2000000);
    packet(f, 0x15, 0, bytes, 6);
    g_assert_cmphex(bytes[1], ==, 0);
    g_assert_cmphex(bytes[2], ==, 0);
    g_assert_cmpuint(count(f, "configuration-generation"), ==, 1);
    memset(bytes, 0, sizeof(bytes));
    stl_le_p(bytes + 1, 0x12345678);
    packet(f, 0x21, 0, bytes, 6);
    memset(bytes, 0, sizeof(bytes));
    packet(f, 0x27, 0, bytes, 6);
    g_assert_cmphex(ldl_le_p(bytes + 1), ==, 0x12345678);

    memset(bytes, 0, sizeof(bytes));
    packet(f, 0x13, 0, bytes, 14);
    device_cold_reset(f->lens);
    advance(2000000);
    g_assert_cmpuint(count(f, "configuration-generation"), ==, 0);
    g_assert_cmpuint(count(f, "completed"), ==, 0);
    cleanup(f);
}

static void test_mode_exchange(void)
{
    LensFixture *f = fixture();
    uint8_t bytes[258] = { 0 };
    g_autofree char *runtime = NULL;

    for (unsigned mode = 0; mode < 3; mode++) {
        memset(bytes, 0, sizeof(bytes));
        bytes[1] = mode;
        packet(f, 0x50, 0, bytes, 4);
        g_assert_cmpuint(count(f, "mode-generation"), ==, mode);
        memset(bytes, 0, sizeof(bytes));
        packet(f, 0x51, 0, bytes, 4);
        g_assert_cmphex(bytes[1], ==, 0x80);
        g_assert_cmpuint(count(f, "mode-generation"), ==, mode + 1);
        memset(bytes, 0, sizeof(bytes));
        packet(f, 0x51, 0, bytes, 4);
        g_assert_cmphex(bytes[1], ==, 0);
        g_assert_cmpuint(count(f, "configuration-generation"), ==, 0);
    }
    memset(bytes, 0, sizeof(bytes));
    packet(f, 0x13, 0, bytes, 14);
    advance(2000000);
    memset(bytes, 0, sizeof(bytes));
    bytes[1] = 1;
    packet(f, 0x16, 0, bytes, 4);
    memset(bytes, 0, sizeof(bytes));
    packet(f, 0x50, 0, bytes, 4);
    advance(2000000);
    g_assert_cmpuint(count(f, "configuration-generation"), ==, 1);
    g_assert_cmpuint(count(f, "mode-generation"), ==, 4);
    runtime = object_property_get_str(OBJECT(f->lens), "runtime", &error_abort);
    g_assert_nonnull(strstr(runtime, "configured=1 pending=0 stream=1"));
    memset(bytes, 0, sizeof(bytes));
    packet(f, 0x20, 0, bytes, sizeof(bytes));
    g_assert_cmphex(bytes[169], ==, 0x40);
    g_assert_cmphex(bytes[216], ==, 3);
    cleanup(f);
}

static void test_mode_boundaries(void)
{
    static const uint8_t invalid[][2] = { { 3, 0 }, { 4, 0 },
                                         { 0x80, 0 }, { 0, 1 } };
    LensFixture *f = fixture();
    uint8_t bytes[4] = { 0 };

    for (unsigned i = 0; i < ARRAY_SIZE(invalid); i++) {
        uint8_t header[] = { 0x55, 0, 1, 0x50, 0, 1, 0, 0 };
        uint8_t invalid_bytes[] = { 0x55, invalid[i][0], invalid[i][1], 0 };
        unsigned falling;

        device_cold_reset(f->lens);
        checksum(header, sizeof(header));
        exchange(f, header, sizeof(header));
        advance(1000000);
        falling = f->falling;
        checksum(invalid_bytes, sizeof(invalid_bytes));
        exchange(f, invalid_bytes, sizeof(invalid_bytes));
        advance(10000000);
        g_assert_cmpuint(f->falling, ==, falling);
        g_assert_cmpuint(count(f, "rejected"), ==, 1);
        g_assert_cmpuint(count(f, "mode-generation"), ==, 0);
    }
    device_cold_reset(f->lens);

    packet(f, 0x50, 0, bytes, sizeof(bytes));
    device_cold_reset(f->lens);
    advance(10000000);
    g_assert_cmpuint(count(f, "mode-generation"), ==, 0);
    memset(bytes, 0, sizeof(bytes));
    packet(f, 0x50, 0, bytes, sizeof(bytes));
    qdev_unrealize(f->lens);
    advance(10000000);
    g_assert_cmpuint(count(f, "mode-generation"), ==, 0);
    cleanup(f);
}

static void test_unrealize_pending(void)
{
    LensFixture *f = fixture();
    uint8_t bytes[14] = { 0, 1 };
    uint8_t header[] = { 0x55, 0, 1, 0x20, 0, 128, 0, 0 };
    unsigned falling;

    packet(f, 0x16, 0, bytes, 4);
    memset(bytes, 0, sizeof(bytes));
    packet(f, 0x13, 0, bytes, 14);
    g_assert_cmpuint(count(f, "configuration-generation"), ==, 0);
    checksum(header, sizeof(header));
    exchange(f, header, sizeof(header));
    falling = f->falling;
    qdev_unrealize(f->lens);
    /* Keep the object alive: finalization cannot mask a pending callback. */
    advance(10000000);
    g_assert_true(f->level);
    g_assert_cmpuint(f->falling, ==, falling);
    g_assert_cmpuint(count(f, "configuration-generation"), ==, 0);
    g_assert_cmpuint(count(f, "stream-frames"), ==, 0);
    cleanup(f);
}

static void test_negative_packets(void)
{
    static const struct {
        uint8_t marker, command;
        unsigned size;
    } invalid[] = {
        { 0x55, 0x10, 26 }, { 0x55, 0x99, 28 }, { 0x54, 0x10, 28 },
    };
    LensFixture *f = fixture();
    uint8_t header[8], bytes[28];

    for (unsigned i = 0; i < ARRAY_SIZE(invalid); i++) {
        device_cold_reset(f->lens);
        memset(header, 0, sizeof(header));
        header[0] = invalid[i].marker;
        header[3] = invalid[i].command;
        stw_le_p(header + 5, (invalid[i].size - 2) / 2);
        checksum(header, sizeof(header));
        exchange(f, header, sizeof(header));
        advance(10000000);
        g_assert_true(f->level);
        g_assert_cmpuint(count(f, "rejected"), ==, 1);
        g_assert_cmpuint(count(f, "completed"), ==, 0);
    }
    device_cold_reset(f->lens);
    memset(header, 0, sizeof(header));
    header[0] = 0x55;
    header[3] = 0x10;
    stw_le_p(header + 5, 13);
    checksum(header, sizeof(header));
    exchange(f, header, sizeof(header));
    advance(1000000);
    memset(bytes, 0, sizeof(bytes));
    bytes[0] = 0x55; /* Deliberately no payload checksum. */
    exchange(f, bytes, sizeof(bytes));
    advance(10000000);
    g_assert_true(f->level);
    g_assert_cmpuint(count(f, "rejected"), ==, 1);
    g_assert_cmpuint(count(f, "completed"), ==, 0);
    cleanup(f);
}

static void stream_write(LensFixture *f, hwaddr offset, uint32_t value)
{
    MemoryRegion *regs = sysbus_mmio_get_region(SYS_BUS_DEVICE(f->stream), 0);

    g_assert_cmpint(memory_region_dispatch_write(regs, offset, value,
                                                MO_32, MEMTXATTRS_UNSPECIFIED),
                    ==, MEMTX_OK);
}

static uint32_t stream_read(LensFixture *f, hwaddr offset)
{
    MemoryRegion *regs = sysbus_mmio_get_region(SYS_BUS_DEVICE(f->stream), 0);
    uint64_t value;

    g_assert_cmpint(memory_region_dispatch_read(regs, offset, &value,
                                               MO_32, MEMTXATTRS_UNSPECIFIED),
                    ==, MEMTX_OK);
    return value;
}

static void test_stream_lifecycle(void)
{
    LensFixture *f = fixture();
    uint8_t request[4] = { 0, 1 }, frame[42];
    unsigned sum = 0;
    uint64_t frames;

    packet(f, 0x16, 0, request, sizeof(request));
    advance(2000000);
    g_assert_cmpuint(count(f, "stream-frames"), ==, 0);
    stream_write(f, 0x60, 0);
    stream_write(f, 0x10, 0xf00);
    stream_write(f, 0x64, 1);
    stream_write(f, 0x3c, 1);
    advance(2000000);
    g_assert_cmpuint(count(f, "stream-frames"), >=, 1);
    g_assert_cmpuint(stream_read(f, 0x30), >=, 21);
    for (unsigned i = 0; i < sizeof(frame); i += 2) {
        stw_le_p(frame + i, stream_read(f, 4));
    }
    for (unsigned i = 0; i < sizeof(frame); i++) {
        sum += frame[i];
    }
    g_assert_cmphex(frame[0], ==, 0x55);
    g_assert_cmphex(sum & 0xff, ==, 0);
    g_assert_cmphex(ldl_le_p(frame + 2), ==, 0);
    memset(request, 0, sizeof(request));
    packet(f, 0x16, 0, request, sizeof(request));
    frames = count(f, "stream-frames");
    advance(10000000);
    g_assert_cmpuint(count(f, "stream-frames"), ==, frames);
    device_cold_reset(f->lens);
    advance(10000000);
    g_assert_cmpuint(count(f, "stream-frames"), ==, 0);
    cleanup(f);
}

static void partial_stream_restart(bool pause)
{
    LensFixture *f = fixture();
    uint8_t request[4] = { 0, 1 }, frame[42];
    unsigned sum = 0;

    stream_write(f, 0x60, 0);
    stream_write(f, 0x10, 0xf00);
    stream_write(f, 0x64, 1);
    stream_write(f, 0x3c, 1);
    /* Leave only five receiver slots: latency cannot hide a partial record. */
    for (unsigned i = 0; i < 27; i++) {
        g_assert_true(nikon_spi_slave_clock(f->stream, 0xabcd, 16, NULL));
    }
    packet(f, 0x16, 0, request, sizeof(request));
    advance(2000000);
    g_assert_cmpuint(stream_read(f, 0x30), ==, 32);
    g_assert_cmpuint(count(f, "stream-frames"), ==, 0);
    if (pause) {
        memset(request, 0, sizeof(request));
        packet(f, 0x16, 0, request, sizeof(request));
        g_assert_cmpuint(stream_read(f, 0x30), ==, 32);
        g_assert_cmpuint(count(f, "stream-frames"), ==, 0);
    }
    memset(request, 0, sizeof(request));
    request[1] = 1;
    packet(f, 0x16, 0, request, sizeof(request));
    g_assert_cmpuint(count(f, "stream-frames"), ==, 0);
    for (unsigned i = 0; i < 27; i++) {
        g_assert_cmphex(stream_read(f, 4), ==, 0xabcd);
    }
    advance(2000000);
    g_assert_cmpuint(stream_read(f, 0x30), >=, 21);
    for (unsigned i = 0; i < sizeof(frame); i += 2) {
        stw_le_p(frame + i, stream_read(f, 4));
    }
    for (unsigned i = 0; i < sizeof(frame); i++) {
        sum += frame[i];
    }
    g_assert_cmphex(frame[0], ==, 0x55);
    g_assert_cmphex(sum & 0xff, ==, 0);
    cleanup(f);
}

static void test_partial_stream_restart(void)
{
    partial_stream_restart(true);
}

static void test_partial_stream_repeat(void)
{
    partial_stream_restart(false);
}

static void focus_request(LensFixture *f, uint8_t sub, int32_t target)
{
    uint8_t bytes[14] = { 0 };

    bytes[1] = sub;
    stl_le_p(bytes + 2, (uint32_t)target);
    packet(f, 0x70, 0, bytes, sizeof(bytes));
}

static void focus_stop(LensFixture *f, uint8_t sub)
{
    uint8_t bytes[4] = { 0 };

    packet(f, 0x78, sub, bytes, sizeof(bytes));
}

static void focus_query(LensFixture *f, int32_t *pos, bool *busy)
{
    uint8_t bytes[14] = { 0 };

    packet(f, 0x71, 0, bytes, sizeof(bytes));
    *busy = (bytes[1] & 2) != 0;
    *pos = (int32_t)ldl_le_p(bytes + 2);
}

static void focus_status(LensFixture *f, int32_t position, bool busy)
{
    uint8_t bytes[258] = { 0 };
    int64_t prop_pos;
    bool prop_busy;

    prop_pos = object_property_get_int(OBJECT(f->lens), "focus-position",
                                       &error_abort);
    g_assert_cmpint(prop_pos, ==, position);
    prop_busy = object_property_get_bool(OBJECT(f->lens), "focus-pending",
                                         &error_abort);
    g_assert_cmpint(prop_busy, ==, busy);
    packet(f, 0x20, 0, bytes, sizeof(bytes));
    g_assert_cmpint((int16_t)lduw_le_p(bytes + 201), ==, (int16_t)position);
    g_assert_cmphex(bytes[203] & 0x02, ==, busy ? 0x02 : 0);
}

static void test_focus_idle(void)
{
    LensFixture *f = fixture();
    uint8_t bytes[14] = { 0 };
    int32_t pos;
    bool busy;

    packet(f, 0x13, 0, bytes, sizeof(bytes));
    advance(2000000);

    g_assert_cmpint(count(f, "focus-noops"), ==, 0);
    g_assert_cmpint(count(f, "focus-generation"), ==, 0);
    focus_status(f, 0, false);
    focus_query(f, &pos, &busy);
    g_assert_cmpint(pos, ==, 0);
    g_assert_false(busy);

    /* Zero move is a noop */
    focus_request(f, 0, 0);
    g_assert_cmpint(count(f, "focus-noops"), ==, 1);
    g_assert_cmpint(count(f, "focus-generation"), ==, 0);
    focus_status(f, 0, false);

    device_cold_reset(f->lens);
    g_assert_cmpint(count(f, "focus-noops"), ==, 0);
    cleanup(f);
}

static void test_focus_motion(void)
{
    LensFixture *f = fixture();
    uint8_t bytes[14] = { 0 };
    int32_t pos;
    bool busy;

    packet(f, 0x13, 0, bytes, sizeof(bytes));
    advance(2000000);

    /* Move 1000 steps at 100 µs/step = 100 ms */
    focus_request(f, 0, 1000);
    g_assert_cmpuint(count(f, "focus-generation"), ==, 0);
    /* 1 ms elapsed in focus_request: pos = 10 */
    focus_status(f, 10, true);

    /* Advance 47 ms: elapsed = 50 ms, pos = 500 */
    advance(47000000);
    focus_status(f, 500, true);

    /* Advance to 99.999999 ms: elapsed from 52 ms is 47.999999 ms */
    advance(47999999);
    g_assert_cmpuint(count(f, "focus-generation"), ==, 0);
    advance(1);
    g_assert_cmpuint(count(f, "focus-generation"), ==, 1);
    focus_status(f, 1000, false);
    focus_query(f, &pos, &busy);
    g_assert_cmpint(pos, ==, 1000);
    g_assert_false(busy);

    /* Relative move: subcommand 1 with -500 */
    focus_request(f, 1, -500);
    advance(49000000);
    g_assert_cmpuint(count(f, "focus-generation"), ==, 2);
    focus_status(f, 500, false);

    /* Minimum move duration: 1 step = 2 ms minimum */
    focus_request(f, 0, 501);
    advance(999999);
    g_assert_cmpuint(count(f, "focus-generation"), ==, 2);
    advance(1);
    g_assert_cmpuint(count(f, "focus-generation"), ==, 3);
    focus_status(f, 501, false);

    cleanup(f);
}

static void read_stream_frame(LensFixture *f, uint8_t *frame)
{
    while (stream_read(f, 0x30) > 0) {
        stream_read(f, 4);
    }
    for (;;) {
        uint16_t word;
        unsigned sum = 0;

        while (stream_read(f, 0x30) < 1) {
            advance(100000);
        }
        word = stream_read(f, 4);
        if ((word & 0xff) == 0x55) {
            stw_le_p(frame, word);
            for (unsigned i = 2; i < 42; i += 2) {
                while (stream_read(f, 0x30) < 1) {
                    advance(100000);
                }
                stw_le_p(frame + i, stream_read(f, 4));
            }
            for (unsigned i = 0; i < 42; i++) {
                sum += frame[i];
            }
            if ((sum & 0xff) == 0) {
                return;
            }
        }
    }
}

static void test_focus_endpoints(void)
{
    LensFixture *f = fixture();
    uint8_t bytes[42] = { 0 };
    uint8_t stream_req[4] = { 0, 1, 0, 0 };

    stream_write(f, 0x60, 0);
    stream_write(f, 0x10, 0xf00);
    stream_write(f, 0x64, 1);
    stream_write(f, 0x3c, 1);

    packet(f, 0x13, 0, bytes, 14);
    advance(2000000);

    /* Enable stream */
    packet(f, 0x16, 0, stream_req, sizeof(stream_req));

    /* Move to near limit: +4096 */
    focus_request(f, 0, 4096);
    advance(410000000);
    focus_status(f, 4096, false);

    read_stream_frame(f, bytes);
    g_assert_cmpint((int32_t)ldl_le_p(bytes + 2), ==, 4096);
    g_assert_cmpuint(bytes[7], ==, 1);

    /* Move to infinity limit: -4096 */
    focus_request(f, 0, -4096);
    advance(820000000);
    focus_status(f, -4096, false);

    read_stream_frame(f, bytes);
    g_assert_cmpint((int32_t)ldl_le_p(bytes + 2), ==, -4096);
    g_assert_cmpuint(bytes[7], ==, 2);

    /* Move back to center (0) */
    focus_request(f, 0, 0);
    advance(410000000);
    focus_status(f, 0, false);

    read_stream_frame(f, bytes);
    g_assert_cmpint((int32_t)ldl_le_p(bytes + 2), ==, 0);
    g_assert_cmpuint(bytes[7], ==, 0);

    cleanup(f);
}

static void test_focus_stop(void)
{
    LensFixture *f = fixture();
    uint8_t bytes[14] = { 0 };

    packet(f, 0x13, 0, bytes, sizeof(bytes));
    advance(2000000);

    /* Start move to 4000 (takes 400 ms) */
    focus_request(f, 0, 4000);
    /* Advance to 99 ms (elapsed = 100 ms at stop header) */
    advance(98000000);

    /* Cancel via 0x78:00 (header reaches at 100 ms -> pos = 1000) */
    focus_stop(f, 0);
    g_assert_cmpuint(count(f, "focus-generation"), ==, 1);
    focus_status(f, 1000, false);

    /* Advance another 100 ms; pos remains 1000 */
    advance(100000000);
    focus_status(f, 1000, false);
    g_assert_cmpuint(count(f, "focus-generation"), ==, 1);

    /* Stop variants 1 and 2 when already stopped */
    focus_stop(f, 1);
    focus_stop(f, 2);
    focus_status(f, 1000, false);
    g_assert_cmpuint(count(f, "focus-generation"), ==, 1);

    cleanup(f);
}

static void reject_packet(LensFixture *f, uint8_t command, uint8_t sub,
                          uint8_t *payload, unsigned size)
{
    uint8_t header[] = { 0x55, 1, 1, command, sub, 0, 0, 0 };
    unsigned falling = f->falling;
    uint64_t rejected = count(f, "rejected");

    stw_le_p(header + 5, (size - 2) / 2);
    checksum(header, sizeof(header));
    exchange(f, header, sizeof(header));
    advance(1000000);
    if (count(f, "rejected") == rejected + 1) {
        g_assert_true(f->level);
        return;
    }
    g_assert_false(f->level);
    falling = f->falling;
    payload[0] = 0x55;
    checksum(payload, size);
    exchange(f, payload, size);
    advance(1000000);
    g_assert_true(f->level);
    g_assert_cmpuint(f->falling, ==, falling);
    g_assert_cmpuint(count(f, "rejected"), ==, rejected + 1);
}

static void test_focus_rejection(void)
{
    LensFixture *f = fixture();
    uint8_t bytes[14] = { 0 };

    /* 1. Unconfigured rejection */
    reject_packet(f, 0x70, 0, bytes, 14);

    /* 2. Configure */
    device_cold_reset(f->lens);
    packet(f, 0x13, 0, bytes, 14);
    advance(2000000);

    /* 3. Target > 4096 */
    memset(bytes, 0, sizeof(bytes));
    stl_le_p(bytes + 2, 4097);
    reject_packet(f, 0x70, 0, bytes, 14);

    /* 4. Target < -4096 */
    device_cold_reset(f->lens);
    packet(f, 0x13, 0, bytes, 14);
    advance(2000000);
    memset(bytes, 0, sizeof(bytes));
    stl_le_p(bytes + 2, (uint32_t)-4097);
    reject_packet(f, 0x70, 0, bytes, 14);

    /* 5. Subcommand > 3 */
    device_cold_reset(f->lens);
    packet(f, 0x13, 0, bytes, 14);
    advance(2000000);
    memset(bytes, 0, sizeof(bytes));
    bytes[1] = 4;
    reject_packet(f, 0x70, 0, bytes, 14);

    /* 6. Stop subcommand >= 3 (header rejection) */
    device_cold_reset(f->lens);
    memset(bytes, 0, sizeof(bytes));
    reject_packet(f, 0x78, 3, bytes, 4);

    /* 7. Overlapping move while focus_pending */
    device_cold_reset(f->lens);
    packet(f, 0x13, 0, bytes, 14);
    advance(2000000);
    focus_request(f, 0, 1000);
    memset(bytes, 0, sizeof(bytes));
    stl_le_p(bytes + 2, 500);
    reject_packet(f, 0x70, 0, bytes, 14);

    cleanup(f);
}

static void test_focus_lifecycle(void)
{
    LensFixture *f = fixture();
    uint8_t bytes[14] = { 0 };
    g_autofree char *runtime = NULL;

    packet(f, 0x13, 0, bytes, sizeof(bytes));
    advance(2000000);

    /* Start move to 2000 (takes 200 ms) */
    focus_request(f, 0, 2000);
    advance(49000000); /* 50 ms -> pos = 500 */
    runtime = object_property_get_str(OBJECT(f->lens), "runtime", &error_abort);
    g_assert_nonnull(strstr(runtime, "focus=500 target=2000 focus-pending=1"));

    /* Cold reset resets position to 0 and clears pending */
    device_cold_reset(f->lens);
    advance(200000000);
    g_assert_cmpuint(count(f, "focus-generation"), ==, 0);
    focus_status(f, 0, false);

    /* Reconfigure and start another move */
    packet(f, 0x13, 0, bytes, 14);
    advance(2000000);
    focus_request(f, 0, 2000);
    advance(49000000); /* 50 ms -> pos = 500 */

    /* Unrealize freezes position and clears pending */
    qdev_unrealize(f->lens);
    advance(200000000);
    g_clear_pointer(&runtime, g_free);
    runtime = object_property_get_str(OBJECT(f->lens), "runtime", &error_abort);
    g_assert_nonnull(strstr(runtime, "focus=500 target=500 focus-pending=0"));

    cleanup(f);
}

int main(int argc, char **argv)
{
    g_autofree char *directory = g_dir_make_tmp("nikon-lens-XXXXXX", NULL);
    g_autofree char *socket = g_build_filename(directory, "qtest", NULL);
    g_autofree char *chardev = g_strdup_printf("unix:%s,server=on,wait=off",
                                               socket);
    const char *const arguments[] = {
        argv[0], "-machine", "none", "-accel", "qtest", "-display", "none",
        "-nodefaults", "-qtest", chardev, NULL
    };
    g_auto(GStrv) qemu_argv = g_new0(char *, G_N_ELEMENTS(arguments));
    int result;

    g_test_init(&argc, &argv, NULL);
    for (unsigned i = 0; i < G_N_ELEMENTS(arguments) - 1; i++) {
        qemu_argv[i] = g_strdup(arguments[i]);
    }
    qemu_init(G_N_ELEMENTS(arguments) - 1, qemu_argv);
    vm_start();
    g_test_add_func("/nikon/lens/initialization", test_initialization);
    g_test_add_func("/nikon/lens/operating-state", test_operating_state);
    g_test_add_func("/nikon/lens/header-rejection", test_rejected_header);
    g_test_add_func("/nikon/lens/reset-partial", test_reset_partial);
    g_test_add_func("/nikon/lens/phase-deadline", test_phase_deadline);
    g_test_add_func("/nikon/lens/state-and-sync", test_state_and_sync);
    g_test_add_func("/nikon/lens/mode-exchange", test_mode_exchange);
    g_test_add_func("/nikon/lens/mode-boundaries", test_mode_boundaries);
    g_test_add_func("/nikon/lens/unrealize-pending", test_unrealize_pending);
    g_test_add_func("/nikon/lens/aperture-idle", test_aperture_idle);
    g_test_add_func("/nikon/lens/aperture-motion", test_aperture_motion);
    g_test_add_func("/nikon/lens/aperture-encodings", test_aperture_encodings);
    g_test_add_func("/nikon/lens/aperture-lifecycle", test_aperture_lifecycle);
    g_test_add_func("/nikon/lens/aperture-rejection", test_aperture_rejection);
    g_test_add_func("/nikon/lens/negative-packets", test_negative_packets);
    g_test_add_func("/nikon/lens/stream-lifecycle", test_stream_lifecycle);
    g_test_add_func("/nikon/lens/partial-stream-restart",
                    test_partial_stream_restart);
    g_test_add_func("/nikon/lens/partial-stream-repeat",
                    test_partial_stream_repeat);
    g_test_add_func("/nikon/lens/focus-idle", test_focus_idle);
    g_test_add_func("/nikon/lens/focus-motion", test_focus_motion);
    g_test_add_func("/nikon/lens/focus-endpoints", test_focus_endpoints);
    g_test_add_func("/nikon/lens/focus-stop", test_focus_stop);
    g_test_add_func("/nikon/lens/focus-rejection", test_focus_rejection);
    g_test_add_func("/nikon/lens/focus-lifecycle", test_focus_lifecycle);
    result = g_test_run();
    qemu_cleanup(result);
    unlink(socket);
    rmdir(directory);
    return result;
}

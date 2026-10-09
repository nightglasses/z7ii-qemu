/* Supplied opaque sensor frames, causal exposure and real DMA readout.
 * Synthetic geometry only; not a Nikon register or physical CMOS fixture.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "qobject/qdict.h"
#include "../libqtest.h"
#include "hw/nikon/sensor.h"

#define RAM 0xb1800000
#define SENSOR_PATH "/machine/peripheral/sensor"
#define SDES_PATH "/machine/sdes"

static char *image, *frame;

static void input(unsigned length)
{
    g_autofree uint8_t *bytes = g_malloc(length);
    unsigned i;

    for (i = 0; i < length; i++) {
        bytes[i] = i * 17 + 3;
    }
    g_assert_true(g_file_set_contents(frame, (char *)bytes, length, NULL));
}

static QTestState *start(const char *configuration, bool connected)
{
    g_autofree char *bios = g_shell_quote(image);
    g_autofree char *path = g_shell_quote(frame);
    g_autofree char *backend = connected ? g_strdup_printf(
        "frame=%s,", path) :
        g_strdup("");
    g_autofree char *args = g_strdup_printf(
        "-M z7ii-mas-diagnostic -smp 1 -bios %s -serial none "
        "-device nikon-sensor,id=sensor,%s%s",
        bios, backend, configuration);
    QTestState *q = qtest_init(args);

    qtest_irq_intercept_out(q, SENSOR_PATH);
    return q;
}

static uint64_t property(QTestState *q, const char *name)
{
    QDict *response = qtest_qmp(q,
        "{'execute':'qom-get','arguments':{'path':%s,'property':%s}}",
        SENSOR_PATH, name);
    uint64_t value = qdict_get_int(response, "return");

    qobject_unref(response);
    return value;
}

static void capture(QTestState *q, bool active, bool success)
{
    QDict *response = qtest_qmp(q,
        "{'execute':'qom-set','arguments':{'path':%s,'property':'capture',"
        "'value':%i}}", SENSOR_PATH, active);

    g_assert_cmpint(qdict_haskey(response, "return"), ==, success);
    g_assert_cmpint(qdict_haskey(response, "error"), ==, !success);
    qobject_unref(response);
}

static void test_frame(void)
{
    QTestState *q;
    unsigned row, column;

    input(512);
    q = start("row-bytes=16,rows=32,stride=20,destination=0xb1801000,"
              "exposure-ns=1000,readout-ns=100", true);
    qtest_memset(q, RAM + 0x1000, 0x7e, 640);
    capture(q, true, true);
    g_assert_true(qtest_get_irq(q, NIKON_SENSOR_EXPOSURE_ACTIVE));
    g_assert_false(qtest_get_irq(q, NIKON_SENSOR_FRAME_DONE));
    qtest_clock_step(q, 999);
    g_assert_cmpuint(property(q, "bytes-written"), ==, 0);
    g_assert_cmphex(qtest_readb(q, RAM + 0x1000), ==, 0x7e);
    qtest_clock_step(q, 1);
    g_assert_false(qtest_get_irq(q, NIKON_SENSOR_EXPOSURE_ACTIVE));
    g_assert_true(qtest_get_irq(q, NIKON_SENSOR_READOUT_ACTIVE));
    g_assert_cmpuint(property(q, "bytes-written"), ==, 0);
    qtest_clock_step_next(q);
    g_assert_cmpuint(property(q, "bytes-written"), ==, 16);
    g_assert_false(qtest_get_irq(q, NIKON_SENSOR_FRAME_DONE));
    qtest_clock_step(q, 3200);
    g_assert_cmpuint(property(q, "phase"), ==, NIKON_SENSOR_DONE);
    g_assert_cmpuint(property(q, "bytes-written"), ==, 512);
    g_assert_true(qtest_get_irq(q, NIKON_SENSOR_FRAME_DONE));
    g_assert_false(qtest_get_irq(q, NIKON_SENSOR_FRAME_ERROR));
    g_assert_false(qtest_get_irq(q, NIKON_SENSOR_READOUT_ACTIVE));
    for (row = 0; row < 32; row++) {
        for (column = 0; column < 16; column++) {
            g_assert_cmphex(qtest_readb(q, RAM + 0x1000 + row * 20 + column),
                            ==, (uint8_t)((row * 16 + column) * 17 + 3));
        }
        g_assert_cmphex(qtest_readl(q, RAM + 0x1000 + row * 20 + 16), ==,
                        0x7e7e7e7e);
    }
    capture(q, false, true);
    g_assert_false(qtest_get_irq(q, NIKON_SENSOR_FRAME_DONE));
    /* A second capture consumes the same explicitly supplied input. */
    capture(q, true, true);
    qtest_clock_step(q, 5000);
    g_assert_cmpuint(property(q, "bytes-written"), ==, 512);
    g_assert_true(qtest_get_irq(q, NIKON_SENSOR_FRAME_DONE));
    qtest_quit(q);
}

static void test_chunks(void)
{
    QTestState *q;

    input(8192);
    q = start("row-bytes=8192,rows=1,stride=8192,destination=0xb1801000,"
              "exposure-ns=1000,readout-ns=100", true);
    capture(q, true, true);
    qtest_clock_step_next(q);
    qtest_clock_step_next(q);
    g_assert_cmpuint(property(q, "bytes-written"), ==, 4096);
    g_assert_false(qtest_get_irq(q, NIKON_SENSOR_FRAME_DONE));
    qtest_clock_step_next(q);
    g_assert_cmpuint(property(q, "bytes-written"), ==, 8192);
    g_assert_true(qtest_get_irq(q, NIKON_SENSOR_FRAME_DONE));
    g_assert_cmphex(qtest_readb(q, RAM + 0x2fff), ==,
                    (uint8_t)(8191 * 17 + 3));
    qtest_quit(q);
    /* Byte payloads have no disk-sector padding or synthetic EOF samples. */
    input(51);
    q = start("row-bytes=17,rows=3,stride=20,destination=0xb1801000,"
              "exposure-ns=1000,readout-ns=100", true);
    qtest_memset(q, RAM + 0x1000, 0x7e, 60);
    capture(q, true, true);
    qtest_clock_step(q, 2000);
    g_assert_cmpuint(property(q, "bytes-written"), ==, 51);
    g_assert_true(qtest_get_irq(q, NIKON_SENSOR_FRAME_DONE));
    g_assert_cmphex(qtest_readb(q, RAM + 0x1038), ==,
                    (uint8_t)(50 * 17 + 3));
    g_assert_cmphex(qtest_readb(q, RAM + 0x1039), ==, 0x7e);
    qtest_quit(q);
}

static void test_cancel(void)
{
    QTestState *q;

    input(512);
    q = start("row-bytes=16,rows=32,stride=16,destination=0xb1801000,"
              "exposure-ns=1000,readout-ns=100", true);
    capture(q, true, true);
    capture(q, true, false);
    qtest_clock_step_next(q);
    qtest_clock_step_next(q);
    capture(q, false, true);
    qtest_clock_step(q, 10000);
    g_assert_cmpuint(property(q, "phase"), ==, NIKON_SENSOR_IDLE);
    g_assert_cmpuint(property(q, "bytes-written"), ==, 16);
    g_assert_cmphex(qtest_readl(q, RAM + 0x1010), ==, 0);
    g_assert_false(qtest_get_irq(q, NIKON_SENSOR_FRAME_DONE));
    g_assert_false(qtest_get_irq(q, NIKON_SENSOR_FRAME_ERROR));
    capture(q, true, true);
    qtest_qmp_assert_success(q, "{'execute':'system_reset'}");
    qtest_qmp_eventwait(q, "RESET");
    qtest_clock_step(q, 10000);
    g_assert_cmpuint(property(q, "phase"), ==, NIKON_SENSOR_IDLE);
    g_assert_cmpuint(property(q, "bytes-written"), ==, 0);
    g_assert_false(qtest_get_irq(q, NIKON_SENSOR_FRAME_DONE));
    qtest_quit(q);
}

static void test_invalid(void)
{
    static const char *configurations[] = {
        "row-bytes=16,rows=32,stride=15,destination=0xb1801000,"
        "exposure-ns=1,readout-ns=1",
        "row-bytes=16,rows=32,stride=16,destination=0xffffff00,"
        "exposure-ns=1,readout-ns=1",
        "row-bytes=16,rows=32,stride=16,destination=0xb1801000,"
        "exposure-ns=1,readout-ns=0",
        "row-bytes=16,rows=32,stride=16,destination=0xb1801000,"
        "exposure-ns=1,readout-ns=9223372036854775807",
        "row-bytes=16,rows=31,stride=16,destination=0xb1801000,"
        "exposure-ns=1,readout-ns=1",
    };
    QTestState *q;
    unsigned i;

    input(512);
    for (i = 0; i < ARRAY_SIZE(configurations); i++) {
        q = start(configurations[i], true);
        capture(q, true, false);
        qtest_clock_step(q, 10000);
        g_assert_cmpuint(property(q, "phase"), ==, NIKON_SENSOR_IDLE);
        g_assert_cmpuint(property(q, "bytes-written"), ==, 0);
        g_assert_false(qtest_get_irq(q, NIKON_SENSOR_FRAME_DONE));
        qtest_quit(q);
    }
    q = start("row-bytes=16,rows=32,stride=16,destination=0xb1801000,"
              "exposure-ns=1,readout-ns=1", false);
    capture(q, true, false);
    g_assert_cmpuint(property(q, "bytes-written"), ==, 0);
    qtest_quit(q);
}

static void test_bus_error(void)
{
    QTestState *q;
    unsigned i;
    static const char *destinations[] = { "0xdead0000", "0x10020000" };

    input(512);
    for (i = 0; i < ARRAY_SIZE(destinations); i++) {
        g_autofree char *configuration = g_strdup_printf(
            "row-bytes=16,rows=32,stride=16,destination=%s,"
            "exposure-ns=1,readout-ns=1", destinations[i]);

        q = start(configuration, true);
        capture(q, true, true);
        qtest_clock_step(q, 100);
        g_assert_cmpuint(property(q, "phase"), ==, NIKON_SENSOR_ERROR);
        g_assert_cmpuint(property(q, "bytes-written"), ==, 0);
        g_assert_true(qtest_get_irq(q, NIKON_SENSOR_FRAME_ERROR));
        g_assert_false(qtest_get_irq(q, NIKON_SENSOR_FRAME_DONE));
        qtest_quit(q);
    }
}

static void test_eof(void)
{
    QTestState *q;

    input(512);
    q = start("row-bytes=16,rows=32,stride=16,destination=0xb1801000,"
              "exposure-ns=1000,readout-ns=100", true);
    capture(q, true, true);
    qtest_clock_step_next(q);
    qtest_clock_step_next(q);
    g_assert_cmpint(truncate(frame, 16), ==, 0);
    qtest_clock_step_next(q);
    g_assert_cmpuint(property(q, "phase"), ==, NIKON_SENSOR_ERROR);
    g_assert_cmpuint(property(q, "bytes-written"), ==, 16);
    g_assert_cmphex(qtest_readl(q, RAM + 0x1010), ==, 0);
    g_assert_true(qtest_get_irq(q, NIKON_SENSOR_FRAME_ERROR));
    g_assert_false(qtest_get_irq(q, NIKON_SENSOR_FRAME_DONE));
    qtest_quit(q);
}

static QTestState *sdes_start(void)
{
    g_autofree char *bios = g_shell_quote(image);
    QTestState *q = qtest_initf(
        "-M z7ii-mas-diagnostic -smp 1 -bios %s -serial none", bios);

    qtest_irq_intercept_out_named(q, SDES_PATH, "sysbus-irq");
    return q;
}

static void sdes_word(QTestState *q, unsigned word, uint32_t value)
{
    unsigned bit;

    for (bit = 0; bit < 32; bit++) {
        qtest_set_irq_in(q, SDES_PATH, "status-data", word * 32 + bit,
                         !!(value & (1U << bit)));
    }
}

static void sdes_event(QTestState *q, enum NikonSdesEvent event)
{
    qtest_set_irq_in(q, SDES_PATH, "status-publish", event, 0);
    qtest_set_irq_in(q, SDES_PATH, "status-publish", event, 1);
}

static void test_sdes_configuration(void)
{
    QTestState *q = sdes_start();
    static const struct {
        uint64_t address;
        uint32_t value;
    } fields[] = {
        { 0x20061000, 0x00501234 }, { 0x200610e8, 3 },
        { 0x200610f4, 2 }, { 0x20061014, 0x3fffffff },
        { 0x200610d4, 0x00a5003c }, { 0x20061120, 0xfedcba98 },
        { 0x20061264, 0xb1801002 }, { 0x20061268, 0x30 },
        { 0x20065014, 0x123456 }, { 0x20065414, 0xabcdef },
        { 0x20065104, 1 }, { 0x20065504, 0 },
        { 0x2006513c, 0xffff }, { 0x2006553c, 0x1234 },
        { 0x20065150, 1 }, { 0x20065550, 0 },
    };
    unsigned i;

    for (i = 0; i < ARRAY_SIZE(fields); i++) {
        qtest_writel(q, fields[i].address, fields[i].value);
    }
    for (i = 0; i < ARRAY_SIZE(fields); i++) {
        g_assert_cmphex(qtest_readl(q, fields[i].address), ==, fields[i].value);
    }
    for (i = 0; i < NIKON_SDES_EVENTS; i++) {
        g_assert_false(qtest_get_irq(q, i));
    }
    qtest_quit(q);
}

static void test_sdes_status(void)
{
    QTestState *q = sdes_start();
    static const uint64_t addresses[] = {
        0x20064014, 0x20065108, 0x20065024, 0x20065110,
        0x20065118, 0x2006511c, 0x20065508, 0x20065424,
        0x20065510, 0x20065518, 0x2006551c,
    };
    static const unsigned events[] = { 0, 1, 2, 2, 2, 2, 3, 4, 4, 4, 4 };
    unsigned i;

    for (i = 0; i < ARRAY_SIZE(addresses); i++) {
        sdes_word(q, i, (1U << i) | 0x80000000);
    }
    for (i = 0; i < NIKON_SDES_EVENTS; i++) {
        sdes_event(q, i);
        g_assert_true(qtest_get_irq(q, i));
    }
    for (i = 0; i < ARRAY_SIZE(addresses); i++) {
        uint32_t value = (1U << i) | 0x80000000;

        g_assert_cmphex(qtest_readl(q, addresses[i]), ==, value);
        qtest_writel(q, addresses[i], 1U << i);
        g_assert_cmphex(qtest_readl(q, addresses[i]), ==, 0x80000000);
        g_assert_true(qtest_get_irq(q, events[i]));
    }
    /* A detailed event remains pending until all its independent words ACK. */
    for (i = 0; i < ARRAY_SIZE(addresses); i++) {
        qtest_writel(q, addresses[i], 0x80000000);
        g_assert_cmphex(qtest_readl(q, addresses[i]), ==, 0);
    }
    for (i = 0; i < NIKON_SDES_EVENTS; i++) {
        g_assert_false(qtest_get_irq(q, i));
    }
    qtest_quit(q);
}

static void test_sdes_publication(void)
{
    QTestState *q = sdes_start();

    sdes_word(q, 2, 0x40);
    sdes_event(q, NIKON_SDES_PRIMARY_DETAIL_EVENT);
    g_assert_false(qtest_get_irq(q, NIKON_SDES_PRIMARY_DETAIL_EVENT));
    g_assert_cmphex(qtest_readl(q, 0x20065024), ==, 0);
    sdes_word(q, 3, 0);
    sdes_word(q, 4, 0);
    sdes_word(q, 5, 0);
    sdes_event(q, NIKON_SDES_PRIMARY_DETAIL_EVENT);
    g_assert_true(qtest_get_irq(q, NIKON_SDES_PRIMARY_DETAIL_EVENT));
    qtest_writel(q, 0x20065024, 0x40);
    sdes_event(q, NIKON_SDES_PRIMARY_DETAIL_EVENT);
    g_assert_false(qtest_get_irq(q, NIKON_SDES_PRIMARY_DETAIL_EVENT));
    sdes_word(q, 0, 0x123);
    sdes_event(q, NIKON_SDES_STATUS_EVENT);
    g_assert_true(qtest_get_irq(q, NIKON_SDES_STATUS_EVENT));
    qtest_qmp_assert_success(q, "{'execute':'system_reset'}");
    qtest_qmp_eventwait(q, "RESET");
    g_assert_false(qtest_get_irq(q, NIKON_SDES_STATUS_EVENT));
    g_assert_cmphex(qtest_readl(q, 0x20064014), ==, 0);
    sdes_event(q, NIKON_SDES_STATUS_EVENT);
    g_assert_false(qtest_get_irq(q, NIKON_SDES_STATUS_EVENT));
    qtest_quit(q);
}

int main(int argc, char **argv)
{
    uint32_t idle = cpu_to_le32(0xeafffffe);
    int fd, result;

    g_test_init(&argc, &argv, NULL);
    fd = g_file_open_tmp("nikonsensorbiosXXXXXX", &image, NULL);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    g_assert_true(g_file_set_contents(image, (char *)&idle,
                                     sizeof(idle), NULL));
    fd = g_file_open_tmp("nikonsensorframeXXXXXX", &frame, NULL);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    qtest_add_func("/nikon/sensor/frame", test_frame);
    qtest_add_func("/nikon/sensor/chunks", test_chunks);
    qtest_add_func("/nikon/sensor/cancel", test_cancel);
    qtest_add_func("/nikon/sensor/invalid", test_invalid);
    qtest_add_func("/nikon/sensor/buserror", test_bus_error);
    qtest_add_func("/nikon/sensor/eof", test_eof);
    qtest_add_func("/nikon/sensor/controller/configuration",
                    test_sdes_configuration);
    qtest_add_func("/nikon/sensor/controller/status", test_sdes_status);
    qtest_add_func("/nikon/sensor/controller/publication", test_sdes_publication);
    result = g_test_run();
    unlink(image);
    unlink(frame);
    g_free(image);
    g_free(frame);
    return result;
}

/* Supplied opaque sensor frames, causal exposure and real DMA readout.
 * Synthetic geometry only; not a Nikon register or physical CMOS fixture.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "qobject/qbool.h"
#include "qobject/qdict.h"
#include "../libqtest.h"
#include "hw/nikon/sensor.h"

#define RAM 0xb1800000
#define SENSOR_PATH "/machine/peripheral/sensor"

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
        "-drive if=none,id=frame,format=raw,readonly=on,file=%s", path) :
        g_strdup("");
    g_autofree char *args = g_strdup_printf(
        "-M z7ii-mas-diagnostic -smp 1 -bios %s -serial none %s "
        "-device nikon-sensor,id=sensor,%s%s",
        bios, backend, connected ? "frame=frame," : "", configuration);
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
    QBool *value = qbool_from_bool(active);
    QDict *response = qtest_qmp(q,
        "{'execute':'qom-set','arguments':{'path':%s,'property':'capture',"
        "'value':%p}}", SENSOR_PATH, value);

    g_assert_cmpint(qdict_haskey(response, "return"), ==, success);
    g_assert_cmpint(qdict_haskey(response, "error"), ==, !success);
    qobject_unref(response);
    qobject_unref(value);
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
    result = g_test_run();
    unlink(image);
    unlink(frame);
    g_free(image);
    g_free(frame);
    return result;
}

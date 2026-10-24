/* Supplied opaque sensor frames, causal exposure and real DMA readout.
 * Synthetic geometry only; not a Nikon register or physical CMOS fixture.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "qobject/qdict.h"
#include "../libqtest.h"
#include "fixture.h"
#include "hw/nikon/sensor.h"

#define RAM 0xb1800000
#define SENSOR_PATH "/machine/peripheral/sensor"
#define SDES_PATH "/machine/sdes"
#define I2C2_BASE 0x2c01a000
#define I2C2_PATH "/machine/i2c2"
#define I2C_IRQ_MASK 0x2ff
#define DISPLAY_BANK 0x20140000
#define DISPLAY_RAM 0xb1810000
#define DISPLAY_FRAME_PENDING ((1U << 1) | (1U << 16))

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

static QTestState *start_device(const char *configuration, bool connected)
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
    return qtest_init(args);
}

static QTestState *start(const char *configuration, bool connected)
{
    QTestState *q = start_device(configuration, connected);

    qtest_irq_intercept_out(q, SENSOR_PATH);
    return q;
}

static uint64_t property_at(QTestState *q, const char *path, const char *name)
{
    QDict *response = qtest_qmp(q,
        "{'execute':'qom-get','arguments':{'path':%s,'property':%s}}",
        path, name);
    uint64_t value = qdict_get_int(response, "return");

    qobject_unref(response);
    return value;
}

static uint64_t property(QTestState *q, const char *name)
{
    return property_at(q, SENSOR_PATH, name);
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

static uint32_t eye_i2c_read(QTestState *q, unsigned offset)
{
    return qtest_readl(q, I2C2_BASE + offset);
}

static void eye_i2c_write(QTestState *q, unsigned offset, uint32_t value)
{
    qtest_writel(q, I2C2_BASE + offset, value);
}

static void eye_wait(QTestState *q, uint32_t mask, uint32_t value)
{
    unsigned steps;

    for (steps = 0; steps < 100; steps++) {
        if ((eye_i2c_read(q, 4) & mask) == value) {
            return;
        }
        /* Synchronize with the controller's next finite bus-clock event;
         * fixed nanosecond steps can stop before the TX FIFO is serviced. */
        qtest_clock_step_next(q);
    }
    g_assert_cmphex(eye_i2c_read(q, 4) & mask, ==, value);
}

static QTestState *eye_start(bool endpoint)
{
    g_autofree char *bios = g_shell_quote(image);
    QTestState *q;

    /* The endpoint is attached explicitly so raw-MAS diagnostics retain an
     * empty bus in the other I2C tests and the transaction remains observable. */
    if (endpoint) {
        q = qtest_initf("-M z7ii-mas-diagnostic -smp 1 -bios %s "
                        "-serial none -display none "
                        "-device nikon-eye-sensor-functional,bus=nikon-i2c2,"
                        "address=0x38",
                        bios);
    } else {
        q = qtest_initf("-M z7ii-mas-diagnostic -smp 1 -bios %s "
                        "-serial none -display none", bios);
    }
    qtest_qmp_assert_success(q, "{'execute':'cont'}");
    qtest_irq_intercept_out_named(q, I2C2_PATH, "sysbus-irq");
    eye_i2c_write(q, 0x24, I2C_IRQ_MASK);
    return q;
}

static void eye_clear_irq(QTestState *q)
{
    eye_i2c_write(q, 0x10, I2C_IRQ_MASK);
    g_assert_false(qtest_get_irq(q, 0));
}

static void eye_write(QTestState *q, uint8_t reg, uint8_t value,
                      bool success)
{
    eye_i2c_write(q, 0x10, I2C_IRQ_MASK);
    eye_i2c_write(q, 0, 6);
    eye_i2c_write(q, 0xc, reg);
    eye_i2c_write(q, 0xc, value);
    eye_i2c_write(q, 8, 0x38);
    eye_wait(q, 0x100, 0);
    if (success) {
        g_assert_cmphex(eye_i2c_read(q, 0x10) & 5, ==, 1);
        g_assert_true(qtest_get_irq(q, 0));
    } else {
        g_assert_cmphex(eye_i2c_read(q, 0x10) & 5, ==, 4);
        g_assert_true(qtest_get_irq(q, 0));
    }
    eye_clear_irq(q);
}

static void eye_pointer(QTestState *q, uint8_t reg)
{
    eye_i2c_write(q, 0x10, I2C_IRQ_MASK);
    eye_i2c_write(q, 0, 0x6e);
    eye_i2c_write(q, 0xc, reg);
    eye_i2c_write(q, 0, 0x3e);
    eye_i2c_write(q, 8, 0x38);
    eye_wait(q, 0x40, 0);
    /* HOLD retains the bus, while the transmitted selector batch completes. */
    g_assert_cmphex(eye_i2c_read(q, 0x10) & 5, ==, 1);
    g_assert_cmphex(eye_i2c_read(q, 4) & 0x100, ==, 0x100);
    eye_clear_irq(q);
}

static void eye_read(QTestState *q, uint8_t reg, uint8_t *data, unsigned count)
{
    unsigned steps;

    eye_pointer(q, reg);
    eye_i2c_write(q, 0, 0x3f);
    eye_i2c_write(q, 0x14, count);
    eye_i2c_write(q, 8, 0x38);
    for (steps = 0; steps < 100; steps++) {
        if (!eye_i2c_read(q, 0x14)) {
            break;
        }
        qtest_clock_step(q, 100000);
    }
    g_assert_cmpuint(eye_i2c_read(q, 0x14), ==, 0);
    while (count--) {
        *data++ = eye_i2c_read(q, 0xc);
    }
    eye_i2c_write(q, 0, 0x2f);
    eye_wait(q, 0x100, 0);
    g_assert_cmphex(eye_i2c_read(q, 0x10) & 5, ==, 1);
    eye_clear_irq(q);
}

static void test_eye_sensor(void)
{
    static const struct {
        uint8_t reg;
        uint8_t value;
    } initialization[] = {
        { 0x40, 0x00 }, /* system control: leave the device out of reset */
        { 0x41, 0x03 }, /* mode control */
        { 0x42, 0x12 }, /* ALS integration/gain profile */
        { 0x43, 0x34 }, /* proximity LED/gain profile */
        { 0x4a, 0x00 }, /* interrupt policy remains cold and disconnected */
        { 0x53, 0x05 }, /* signed proximity offset, low byte */
        { 0x54, 0x00 }, /* signed proximity offset, high byte */
    };
    uint8_t data[2];
    QTestState *q = eye_start(true);
    unsigned i;

    /* This is the complete source-selected writable initialization surface. */
    for (i = 0; i < ARRAY_SIZE(initialization); i++) {
        eye_write(q, initialization[i].reg, initialization[i].value, true);
    }
    for (i = 0; i < ARRAY_SIZE(initialization); i++) {
        eye_read(q, initialization[i].reg, data, 1);
        g_assert_cmphex(data[0], ==, initialization[i].value);
    }
    eye_read(q, 0x92, data, 1);
    g_assert_cmphex(data[0], ==, 0xe0);
    eye_read(q, 0x44, data, 2);
    g_assert_cmphex(data[0], ==, 0);
    g_assert_cmphex(data[1], ==, 0);
    eye_read(q, 0x46, data, 2);
    g_assert_cmphex(data[0], ==, 0);
    g_assert_cmphex(data[1], ==, 0);
    eye_read(q, 0x48, data, 2);
    g_assert_cmphex(data[0], ==, 0);
    g_assert_cmphex(data[1], ==, 0);
    qtest_qmp_assert_success(q, "{'execute':'system_reset'}");
    qtest_qmp_eventwait(q, "RESET");
    eye_read(q, 0x40, data, 1);
    g_assert_cmphex(data[0], ==, 0);
    eye_read(q, 0x92, data, 1);
    g_assert_cmphex(data[0], ==, 0xe0);
    eye_read(q, 0x44, data, 2);
    g_assert_cmphex(data[0], ==, 0);
    g_assert_cmphex(data[1], ==, 0);
    qtest_quit(q);
}

static void test_eye_sensor_nak(void)
{
    QTestState *q = eye_start(true);
    uint8_t data;

    /* A valid address with an unqualified register is a real slave NAK. */
    eye_write(q, 0x45, 0, false);
    /* Read-only ID registers also reject writes and retain their value. */
    eye_write(q, 0x92, 0x12, false);
    eye_write(q, 0x92, 0x34, false);
    eye_read(q, 0x92, &data, 1);
    g_assert_cmphex(data, ==, 0xe0);
    /* The board address itself remains NAK when no eye endpoint is present. */
    qtest_quit(q);
    q = eye_start(false);
    eye_write(q, 0x40, 0, false);
    qtest_quit(q);
}

static void display_input(void)
{
    uint8_t bytes[8 * 8 * 4];
    unsigned x, y;

    for (y = 0; y < 8; y++) {
        for (x = 0; x < 8; x++) {
            stl_le_p(bytes + (y * 8 + x) * 4, 0xff12e534);
        }
    }
    g_assert_true(g_file_set_contents(frame, (char *)bytes, sizeof(bytes),
                                      NULL));
}

static void display_packet(QTestState *q)
{
    unsigned bytes = 4;

    qtest_writel(q, DISPLAY_BANK + 0x1100, 1);
    qtest_writel(q, DISPLAY_BANK + 0x1308, (8U << 16) | 8);
    qtest_writel(q, DISPLAY_BANK + 0x1328, 8);
    qtest_writel(q, DISPLAY_BANK + 0x132c, 8);
    qtest_writel(q, DISPLAY_BANK + 0x1c00, 0);
    qtest_writel(q, DISPLAY_BANK + 0x1c04, 0);
    qtest_writel(q, DISPLAY_BANK + 0x2100, 1);
    qtest_writel(q, DISPLAY_BANK + 0x2200, 0);
    qtest_writel(q, DISPLAY_BANK + 0x2204, (8U << 16) | 8);
    qtest_writel(q, DISPLAY_BANK + 0x2210, 0x03000102);
    qtest_writel(q, DISPLAY_BANK + 0x2400, (8U << 16) | 8);
    qtest_writel(q, DISPLAY_BANK + 0x2440, DISPLAY_RAM);
    qtest_writel(q, DISPLAY_BANK + 0x24c0, DISPLAY_RAM + 0x4000);
    qtest_writel(q, DISPLAY_BANK + 0x2500, 8 * bytes + 8);
    qtest_writel(q, DISPLAY_BANK + 0x2540, 8);
    qtest_writel(q, DISPLAY_BANK + 0x2580, 0);
    qtest_writel(q, DISPLAY_BANK + 0x25c0, 1);
}

static void test_sensor_display_buffer(void)
{
    QTestState *q;
    g_autofree char *path = NULL;
    g_autofree char *contents = NULL;
    const char *header = "P6\n8 8\n255\n";
    gsize length;
    QDict *reply;
    unsigned i;
    int fd;

    display_input();
    q = start_device("row-bytes=32,rows=8,stride=40,destination=0xb1810000,"
                     "exposure-ns=1,readout-ns=1", true);
    nikon_intercept_gic(q);
    qtest_memset(q, DISPLAY_RAM, 0xa5, 8 * 40);
    capture(q, true, true);
    qtest_clock_step(q, 100000);
    g_assert_cmpuint(property(q, "phase"), ==, NIKON_SENSOR_DONE);
    g_assert_cmpuint(property(q, "bytes-written"), ==, 8 * 8 * 4);
    for (i = 0; i < 8; i++) {
        g_assert_cmphex(qtest_readl(q, DISPLAY_RAM + i * 40), ==,
                        0xff12e534);
        g_assert_cmphex(qtest_readl(q, DISPLAY_RAM + i * 40 + 32), ==,
                        0xa5a5a5a5);
    }

    /* The destination is the display producer's declared input buffer. */
    display_packet(q);
    qtest_writel(q, DISPLAY_BANK + 0x1200, 1);
    for (i = 0; i < 64 && !qtest_get_irq(q, 111 - 32); i++) {
        qtest_clock_step_next(q);
    }
    g_assert_true(qtest_get_irq(q, 111 - 32));
    fd = g_file_open_tmp("nikon-sensor-displayXXXXXX", &path, NULL);

    g_assert_cmpint(fd, >=, 0);
    close(fd);
    reply = qtest_qmp(q, "{'execute':'screendump','arguments':"
                        "{'filename':%s,'format':'ppm','device':'display',"
                        "'head':0}}", path);
    g_assert_false(qdict_haskey(reply, "error"));
    qobject_unref(reply);
    g_assert_true(g_file_get_contents(path, &contents, &length, NULL));
    g_assert_cmpuint(length, ==, strlen(header) + 64 * 3);
    g_assert_true(g_str_has_prefix(contents, header));
    for (i = 0; i < 64; i++) {
        g_assert_cmphex((uint8_t)contents[strlen(header) + i * 3], ==, 0x12);
        g_assert_cmphex((uint8_t)contents[strlen(header) + i * 3 + 1], ==,
                        0xe5);
        g_assert_cmphex((uint8_t)contents[strlen(header) + i * 3 + 2], ==,
                        0x34);
    }
    unlink(path);
    qtest_writel(q, DISPLAY_BANK + 0x1214, DISPLAY_FRAME_PENDING);
    qtest_quit(q);
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

static void status_word(QTestState *q, const char *path,
                        unsigned word, uint32_t value)
{
    unsigned bit;

    for (bit = 0; bit < 32; bit++) {
        qtest_set_irq_in(q, path, "status-data", word * 32 + bit,
                         !!(value & (1U << bit)));
    }
}

static void sdes_word(QTestState *q, unsigned word, uint32_t value)
{
    status_word(q, SDES_PATH, word, value);
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

    g_assert_cmphex(qtest_readl(q, 0x200610e8), ==, 0);
    g_assert_cmphex(qtest_readl(q, 0x200610f4), ==, 0);
    g_assert_cmphex(qtest_readl(q, 0x20065104), ==, 0);
    g_assert_cmphex(qtest_readl(q, 0x20065504), ==, 0);
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

#define CAPTURE_PATH "/machine/capture-control"
#define CAPTURE_BASE 0x20068000

static QTestState *capture_start(const char *output)
{
    g_autofree char *bios = g_shell_quote(image);
    QTestState *q = qtest_initf(
        "-M z7ii-mas-diagnostic -smp 1 -bios %s -serial none", bios);

    if (output) {
        qtest_irq_intercept_out_named(q, CAPTURE_PATH, output);
    } else {
        qtest_irq_intercept_out_named(q, CAPTURE_PATH, "sysbus-irq");
    }
    return q;
}

static void publish_capture(QTestState *q, unsigned event)
{
    qtest_set_irq_in(q, CAPTURE_PATH, "status-publish", event, 0);
    qtest_set_irq_in(q, CAPTURE_PATH, "status-publish", event, 1);
}

static void test_capture_configuration(void)
{
    QTestState *q = capture_start(NULL);
    static const struct {
        unsigned first, last;
    } ranges[] = {
        { 0x500, 0x500 }, { 0x508, 0x81c }, { 0xa00, 0xafc },
        { 0xc00, 0xe3c }, { 0xf00, 0x100c }, { 0x1100, 0x117c },
    };
    unsigned i, offset;

    g_assert_cmphex(qtest_readl(q, CAPTURE_BASE), ==, 0);
    qtest_writel(q, CAPTURE_BASE + 0x18, 0xfff);
    qtest_writel(q, CAPTURE_BASE + 0xc, 0xc);
    g_assert_cmphex(qtest_readl(q, CAPTURE_BASE + 0x18), ==, 0xfff);
    g_assert_cmphex(qtest_readl(q, CAPTURE_BASE + 0xc), ==, 0);
    for (i = 0; i < 8; i++) {
        uint64_t mask = CAPTURE_BASE + 0x200 + i * 0x20;

        g_assert_cmphex(qtest_readl(q, mask), ==, 0);
        qtest_writel(q, mask, qtest_readl(q, mask) | (1U << i));
        g_assert_cmphex(qtest_readl(q, mask), ==, 1U << i);
    }
    for (i = 0; i < ARRAY_SIZE(ranges); i++) {
        for (offset = ranges[i].first; offset <= ranges[i].last; offset += 4) {
            qtest_writel(q, CAPTURE_BASE + offset, 0x90000000 | offset);
        }
    }
    for (i = 0; i < ARRAY_SIZE(ranges); i++) {
        for (offset = ranges[i].first; offset <= ranges[i].last; offset += 4) {
            g_assert_cmphex(qtest_readl(q, CAPTURE_BASE + offset), ==,
                            0x90000000 | offset);
        }
    }
    qtest_writel(q, CAPTURE_BASE + 0x100, 1);
    qtest_writel(q, CAPTURE_BASE + 0x104, 0x80);
    g_assert_cmphex(qtest_readl(q, CAPTURE_BASE + 0x100), ==, 1);
    g_assert_cmphex(qtest_readl(q, CAPTURE_BASE + 0x104), ==, 0x80);
    for (i = 0; i < NIKON_CAPTURE_EVENTS; i++) {
        g_assert_false(qtest_get_irq(q, i));
    }
    qtest_quit(q);

    q = capture_start("command-data");
    qtest_writel(q, CAPTURE_BASE + 0x100, 0x12345678);
    qtest_writel(q, CAPTURE_BASE + 0x104, 0x90abcdef);
    for (i = 0; i < 64; i++) {
        uint32_t value = i < 32 ? 0x12345678 : 0x90abcdef;

        g_assert_cmpint(qtest_get_irq(q, i), ==,
                        !!(value & (1U << (i % 32))));
    }
    qtest_qmp_assert_success(q, "{'execute':'system_reset'}");
    qtest_qmp_eventwait(q, "RESET");
    for (i = 0; i < 64; i++) {
        g_assert_false(qtest_get_irq(q, i));
    }
    qtest_quit(q);
}

static void test_capture_packet(void)
{
    static const struct {
        unsigned first, last;
        uint32_t mask;
    } bounded[] = {
        { 0x08, 0x08, 0xfffff }, { 0x0c, 0x20, 0xffff },
        { 0xcc, 0xcc, 0xffffff }, { 0x174, 0x174, 0xffffff },
        { 0x1d4, 0x1d4, 0xffff }, { 0x1d8, 0x1d8, 0xfffff },
        { 0x1dc, 0x1e4, 0xffff }, { 0x1e8, 0x1e8, 0xffffff },
        { 0x1f8, 0x1f8, 0xffff00ff }, { 0x1fc, 0x1fc, 0xff },
        { 0x200, 0x200, 0x11 },
    };
    static const uint32_t patterns[] = {
        UINT32_MAX, 0xa5a55a5a, 0x01020304, 0x80000000, 0,
    };
    const uint32_t base = 0x20069d00;
    uint32_t expected[0x204 / 4] = { 0 };
    QTestState *q = capture_start(NULL);

    /* Original first rejected scalar; no firmware packet array is embedded. */
    qtest_writel(q, base, 0x03960000);
    g_assert_cmphex(qtest_readl(q, base), ==, 0x03960000);
    qtest_writel(q, CAPTURE_BASE + 0xd00, 0xfeedc0de);
    qtest_writel(q, 0x20069850, 0x01234567);
    for (unsigned i = 0; i < 8; i++) {
        qtest_writel(q, CAPTURE_BASE + 0x200 + i * 0x20, UINT32_MAX);
    }
    for (unsigned pass = 0; pass < ARRAY_SIZE(patterns); pass++) {
        for (unsigned offset = 0; offset < sizeof(expected); offset += 4) {
            uint32_t mask = UINT32_MAX;

            for (unsigned i = 0; i < ARRAY_SIZE(bounded); i++) {
                if (offset >= bounded[i].first && offset <= bounded[i].last) {
                    mask = bounded[i].mask;
                    break;
                }
            }
            expected[offset / 4] = (patterns[pass] ^ (offset * 0x01010101)) & mask;
            qtest_writel(q, base + offset, expected[offset / 4]);
            g_assert_cmphex(qtest_readl(q, base + offset), ==,
                            expected[offset / 4]);
            /* Every unknown bit rejects the write instead of truncating it. */
            for (unsigned bit = 0; bit < 32; bit++) {
                if (!(mask & (1U << bit))) {
                    qtest_writel(q, base + offset,
                                 expected[offset / 4] | (1U << bit));
                    g_assert_cmphex(qtest_readl(q, base + offset), ==,
                                    expected[offset / 4]);
                }
            }
        }
        for (unsigned offset = 0; offset < sizeof(expected); offset += 4) {
            g_assert_cmphex(qtest_readl(q, base + offset), ==,
                            expected[offset / 4]);
        }
    }
    /* RMW bit0/bit4 are independent retained controls, not capture requests. */
    for (unsigned i = 0; i < 4; i++) {
        const uint32_t controls[] = { 1, 0x11, 0x10, 0 };

        qtest_writel(q, base + 0x200, controls[i]);
        g_assert_cmphex(qtest_readl(q, base + 0x200), ==, controls[i]);
    }
    qtest_writeb(q, base, 0xff);
    qtest_writew(q, base + 4, 0xffff);
    qtest_writel(q, base + 1, UINT32_MAX);
    qtest_writel(q, base - 4, UINT32_MAX);
    qtest_writel(q, base + 0x204, UINT32_MAX);
    g_assert_cmphex(qtest_readl(q, base), ==, expected[0]);
    g_assert_cmphex(qtest_readl(q, base + 4), ==, expected[1]);
    g_assert_cmphex(qtest_readl(q, base + 0x200), ==, 0);
    g_assert_cmphex(qtest_readl(q, CAPTURE_BASE + 0xd00), ==, 0xfeedc0de);
    g_assert_cmphex(qtest_readl(q, 0x20069850), ==, 0x01234567);
    qtest_clock_step(q, 1000000);
    g_assert_cmphex(qtest_readl(q, CAPTURE_BASE + 8), ==, 0);
    g_assert_cmpuint(property_at(q, "/machine/readout-control",
                                 "request-generation"), ==, 0);
    for (unsigned i = 0; i < 8; i++) {
        g_assert_cmphex(qtest_readl(q, CAPTURE_BASE + 0x210 + i * 0x20), ==, 0);
    }
    for (unsigned i = 0; i < NIKON_CAPTURE_EVENTS; i++) {
        g_assert_false(qtest_get_irq(q, i));
    }
    qtest_system_reset(q);
    for (unsigned offset = 0; offset < sizeof(expected); offset += 4) {
        g_assert_cmphex(qtest_readl(q, base + offset), ==, 0);
    }
    qtest_clock_step(q, 1000000);
    for (unsigned i = 0; i < NIKON_CAPTURE_EVENTS; i++) {
        g_assert_false(qtest_get_irq(q, i));
    }
    qtest_quit(q);
}

static void endpoint_read_error(QTestState *q, uint32_t address)
{
    g_autofree char *reply = qtest_hmp(q, "xp /1wx 0x%08x", address);

    /* HMP uses address_space_read and reports MEMTX_ERROR, unlike qtest readl. */
    g_assert_nonnull(strstr(reply, "Cannot access memory"));
}

static void test_capture_banks(void)
{
    static const uint32_t bases[] = { 0x2006a000, 0x2006be00 };
    static const uint32_t patterns[] = {
        UINT32_MAX, 0xa55a3cc3, 0x01020304, 0x80000000, 0,
    };
    static const char widths[] = { 'b', 'h' };
    uint32_t expected[2][11];
    QTestState *q = capture_start(NULL);

    qtest_writel(q, bases[0], 0x1234);
    g_assert_cmphex(qtest_readl(q, bases[0]), ==, 0x1234);
    qtest_memset(q, RAM + 0x1000, 0xa7, 32);
    for (unsigned i = 0; i < 8; i++) {
        qtest_writel(q, CAPTURE_BASE + 0x200 + i * 0x20, UINT32_MAX);
    }
    for (unsigned pass = 0; pass < ARRAY_SIZE(patterns); pass++) {
        for (unsigned bank = 0; bank < ARRAY_SIZE(bases); bank++) {
            for (unsigned word = 0; word < ARRAY_SIZE(expected[bank]); word++) {
                unsigned offset = word == 10 ? 0x40 : word * 4;
                uint32_t mask = word < 8 ? 0xffff :
                                word < 10 ? 0xff : UINT32_MAX;
                uint32_t value = patterns[pass] ^ (bank * 0x01010000 + offset);

                value &= mask;
                expected[bank][word] = value;
                qtest_writel(q, bases[bank] + offset, value);
                g_assert_cmphex(qtest_readl(q, bases[bank] + offset), ==,
                                value);
                for (unsigned bit = 0; !pass && bit < 32; bit++) {
                    if (!(mask & (1U << bit))) {
                        qtest_writel(q, bases[bank] + offset,
                                     value | (1U << bit));
                        g_assert_cmphex(qtest_readl(q, bases[bank] + offset),
                                        ==, value);
                    }
                }
            }
        }
        /* Both regions retain separate data, including the unmasked40 word. */
        for (unsigned bank = 0; bank < ARRAY_SIZE(bases); bank++) {
            for (unsigned word = 0; word < ARRAY_SIZE(expected[bank]); word++) {
                unsigned offset = word == 10 ? 0x40 : word * 4;

                g_assert_cmphex(qtest_readl(q, bases[bank] + offset), ==,
                                expected[bank][word]);
            }
        }
    }
    for (unsigned bank = 0; bank < ARRAY_SIZE(bases); bank++) {
        uint32_t base = bases[bank];

        qtest_writeb(q, base, 0xff);
        qtest_writew(q, base, 0xffff);
        qtest_writel(q, base + 1, UINT32_MAX);
        g_assert_cmphex(qtest_readl(q, base), ==, expected[bank][0]);
        for (unsigned i = 0; i < ARRAY_SIZE(widths); i++) {
            g_autofree char *reply = qtest_hmp(q, "xp /1%cx 0x%08x",
                                               widths[i], base);

            g_assert_nonnull(strstr(reply, "Cannot access memory"));
        }
        endpoint_read_error(q, base + 1);
        endpoint_read_error(q, base - 4);
        for (unsigned offset = 0x28; offset <= 0x3c; offset += 4) {
            qtest_writel(q, base + offset, UINT32_MAX);
            endpoint_read_error(q, base + offset);
        }
        qtest_writel(q, base + 0x44, UINT32_MAX);
        endpoint_read_error(q, base + 0x44);
        for (unsigned offset = 0x100; offset <= 0x12c; offset += 4) {
            qtest_writel(q, base + offset, UINT32_MAX);
            endpoint_read_error(q, base + offset);
        }
        g_assert_cmphex(qtest_readl(q, base + 0x40), ==, expected[bank][10]);
    }
    qtest_clock_step(q, 1000000);
    g_assert_cmphex(qtest_readl(q, CAPTURE_BASE + 8), ==, 0);
    for (unsigned i = 0; i < 8; i++) {
        g_assert_cmphex(qtest_readl(q, CAPTURE_BASE + 0x210 + i * 0x20), ==, 0);
    }
    for (unsigned i = 0; i < NIKON_CAPTURE_EVENTS; i++) {
        g_assert_false(qtest_get_irq(q, i));
    }
    g_assert_cmpuint(property_at(q, "/machine/readout-control",
                                 "request-generation"), ==, 0);
    for (unsigned offset = 0; offset < 32; offset += 4) {
        g_assert_cmphex(qtest_readl(q, RAM + 0x1000 + offset), ==, 0xa7a7a7a7);
    }
    qtest_system_reset(q);
    for (unsigned bank = 0; bank < ARRAY_SIZE(bases); bank++) {
        for (unsigned word = 0; word < ARRAY_SIZE(expected[bank]); word++) {
            unsigned offset = word == 10 ? 0x40 : word * 4;

            g_assert_cmphex(qtest_readl(q, bases[bank] + offset), ==, 0);
        }
        endpoint_read_error(q, bases[bank] + 0x28);
        endpoint_read_error(q, bases[bank] + 0x44);
    }
    qtest_clock_step(q, 1000000);
    for (unsigned i = 0; i < NIKON_CAPTURE_EVENTS; i++) {
        g_assert_false(qtest_get_irq(q, i));
    }
    qtest_quit(q);
}

static void test_capture_parameters(void)
{
    static const struct {
        unsigned first, last;
        uint32_t mask;
    } fields[] = {
        { 0, 8, UINT32_MAX }, { 0xc, 0xc, 0xff },
        { 0x10, 0x10, 0xffff }, { 0x14, 0x1c, UINT32_MAX },
        { 0x20, 0x20, 0xff }, { 0x24, 0x24, UINT32_MAX },
        { 0x28, 0x28, 0xffffff }, { 0x2c, 0x2c, 3 },
        { 0x30, 0x30, 0x03ff03ff }, { 0x100, 0x2fc, UINT32_MAX },
    };
    static const uint32_t patterns[] = {
        UINT32_MAX, 0xa55a3cc3, 0x80000000, 0,
    };
    const uint32_t base = 0x20069500;
    uint32_t expected[0x300 / 4] = { 0 };
    bool known[ARRAY_SIZE(expected)] = { false };
    unsigned count = 0;
    QTestState *q = capture_start(NULL);

    qtest_writel(q, base + 0x14, 0x80000006);
    g_assert_cmphex(qtest_readl(q, base + 0x14), ==, 0x80000006);
    qtest_writel(q, 0x20069808, 0x5aa5); /* Independent following device. */
    qtest_memset(q, RAM + 0x1000, 0xa7, 64);
    for (unsigned i = 0; i < 8; i++) {
        qtest_writel(q, CAPTURE_BASE + 0x200 + i * 0x20, UINT32_MAX);
    }
    for (unsigned pass = 0; pass < ARRAY_SIZE(patterns); pass++) {
        for (unsigned i = 0; i < ARRAY_SIZE(fields); i++) {
            for (unsigned offset = fields[i].first; offset <= fields[i].last;
                 offset += 4) {
                unsigned word = offset / 4;
                uint32_t value = (patterns[pass] ^ (offset * 0x01010101)) &
                                 fields[i].mask;

                if (!pass) {
                    g_assert_false(known[word]);
                    known[word] = true;
                    count++;
                }
                expected[word] = value;
                qtest_writel(q, base + offset, value);
                g_assert_cmphex(qtest_readl(q, base + offset), ==, value);
                for (unsigned bit = 0; !pass && bit < 32; bit++) {
                    if (!(fields[i].mask & (1U << bit))) {
                        qtest_writel(q, base + offset, value | (1U << bit));
                        g_assert_cmphex(qtest_readl(q, base + offset), ==, value);
                    }
                }
            }
        }
        for (unsigned word = 0; word < ARRAY_SIZE(expected); word++) {
            if (known[word]) {
                g_assert_cmphex(qtest_readl(q, base + word * 4), ==,
                                expected[word]);
            }
        }
    }
    g_assert_cmpuint(count, ==, 141);
    /* The original swap helper exchanges high halves, retaining low halves. */
    qtest_writel(q, base + 0x18, 0x12345678);
    qtest_writel(q, base + 0x24, 0x9abcdef0);
    uint32_t first = qtest_readl(q, base + 0x18);
    uint32_t second = qtest_readl(q, base + 0x24);

    expected[0x18 / 4] = (second & 0xffff0000) | (first & 0xffff);
    expected[0x24 / 4] = (first & 0xffff0000) | (second & 0xffff);
    qtest_writel(q, base + 0x18, expected[0x18 / 4]);
    qtest_writel(q, base + 0x24, expected[0x24 / 4]);
    g_assert_cmphex(qtest_readl(q, base + 0x18), ==, 0x9abc5678);
    g_assert_cmphex(qtest_readl(q, base + 0x24), ==, 0x1234def0);
    qtest_writel(q, base + 0x2c, 0);
    qtest_writel(q, base + 0x2c, qtest_readl(q, base + 0x2c) | 1);
    qtest_writel(q, base + 0x2c, qtest_readl(q, base + 0x2c) | 2);
    g_assert_cmphex(qtest_readl(q, base + 0x2c), ==, 3);
    qtest_writel(q, base + 0x2c, qtest_readl(q, base + 0x2c) & ~1U);
    g_assert_cmphex(qtest_readl(q, base + 0x2c), ==, 2);
    expected[0x2c / 4] = 0;
    qtest_writel(q, base + 0x2c, 0);
    expected[0x14 / 4] = 0;
    qtest_writel(q, base + 0x14, 0); /* Does not clear other fields/table. */
    for (unsigned word = 0; word < ARRAY_SIZE(known); word++) {
        if (!known[word]) {
            qtest_writel(q, base + word * 4, UINT32_MAX);
            endpoint_read_error(q, base + word * 4);
        }
    }
    qtest_writeb(q, base + 0x14, 0xff);
    qtest_writew(q, base + 0x14, 0xffff);
    qtest_writel(q, base + 0x15, UINT32_MAX);
    endpoint_read_error(q, base + 0x15);
    endpoint_read_error(q, base - 4);
    for (unsigned i = 0; i < 2; i++) {
        g_autofree char *reply = qtest_hmp(q, "xp /1%cx 0x%08x",
                                           i ? 'h' : 'b', base + 0x14);

        g_assert_nonnull(strstr(reply, "Cannot access memory"));
    }
    g_assert_cmphex(qtest_readl(q, CAPTURE_BASE + 8), ==, 0);
    qtest_writel(q, CAPTURE_BASE + 0x100, 0x80);
    qtest_writel(q, CAPTURE_BASE + 0x104, 0x80);
    qtest_writel(q, CAPTURE_BASE + 0x10, 0x200);
    qtest_writel(q, CAPTURE_BASE + 8, 0x200);
    qtest_clock_step(q, 1000000);
    g_assert_cmphex(qtest_readl(q, CAPTURE_BASE + 8), ==, 0x200);
    for (unsigned word = 0; word < ARRAY_SIZE(expected); word++) {
        if (known[word]) {
            g_assert_cmphex(qtest_readl(q, base + word * 4), ==, expected[word]);
        }
    }
    for (unsigned i = 0; i < 8; i++) {
        g_assert_cmphex(qtest_readl(q, CAPTURE_BASE + 0x210 + i * 0x20), ==, 0);
    }
    for (unsigned i = 0; i < NIKON_CAPTURE_EVENTS; i++) {
        g_assert_false(qtest_get_irq(q, i));
    }
    g_assert_cmpuint(property_at(q, "/machine/readout-control",
                                 "request-generation"), ==, 0);
    for (unsigned offset = 0; offset < 64; offset += 4) {
        g_assert_cmphex(qtest_readl(q, RAM + 0x1000 + offset), ==, 0xa7a7a7a7);
    }
    g_assert_cmphex(qtest_readl(q, 0x20069808), ==, 0x5aa5);
    qtest_system_reset(q);
    for (unsigned word = 0; word < ARRAY_SIZE(known); word++) {
        if (known[word]) {
            g_autofree char *reply = qtest_hmp(q, "xp /1wx 0x%08x",
                                               base + word * 4);

            g_assert_null(strstr(reply, "Cannot access memory"));
            g_assert_cmphex(qtest_readl(q, base + word * 4), ==, 0);
        } else {
            endpoint_read_error(q, base + word * 4);
        }
    }
    qtest_clock_step(q, 1000000);
    for (unsigned i = 0; i < NIKON_CAPTURE_EVENTS; i++) {
        g_assert_false(qtest_get_irq(q, i));
    }
    qtest_quit(q);
}

static const uint32_t capture_group_results[] = {
    0x2006bb70, 0x2006bb74, 0x2006bb78, 0x2006bc78,
    0x2006bd78, 0x2006bb7c, 0x2006bc7c, 0x2006bd7c,
};

static void capture_group_result_packet(QTestState *q, bool cold)
{
    for (unsigned i = 0; i < ARRAY_SIZE(capture_group_results); i++) {
        uint32_t address = capture_group_results[i];

        if (cold) {
            g_autofree char *reply = qtest_hmp(q, "xp /1wx 0x%08x", address);

            /* qtest's zero-filled error buffer cannot prove a readable zero. */
            g_assert_null(strstr(reply, "Cannot access memory"));
            g_assert_cmphex(qtest_readl(q, address), ==, 0);
        } else {
            endpoint_read_error(q, address);
        }
    }
}

static void test_capture_group_results(void)
{
    QTestState *q = capture_start(NULL);

    capture_group_result_packet(q, true);
    qtest_writel(q, 0x2006bb44, 0x5a);
    for (unsigned i = 0; i < ARRAY_SIZE(capture_group_results); i++) {
        qtest_writel(q, capture_group_results[i], UINT32_MAX);
    }
    capture_group_result_packet(q, true);
    qtest_writel(q, CAPTURE_BASE + 0x100, 2);
    qtest_writel(q, CAPTURE_BASE + 0x104, 0);
    qtest_writel(q, CAPTURE_BASE + 0x10, 1);
    qtest_writel(q, CAPTURE_BASE + 8, 1); /* Another effective unit. */
    capture_group_result_packet(q, true);
    qtest_writeb(q, CAPTURE_BASE + 0x100, 0x10);
    qtest_writel(q, CAPTURE_BASE + 8, 0x100); /* Reserved unit, rejected. */
    qtest_writel(q, CAPTURE_BASE + 8, 0x20); /* Not enabled. */
    capture_group_result_packet(q, true);
    qtest_writel(q, CAPTURE_BASE, 0x20);
    qtest_writel(q, CAPTURE_BASE + 0x10, 0x20);
    qtest_writel(q, CAPTURE_BASE + 8, 0x20); /* Held in unit reset. */
    capture_group_result_packet(q, true);
    qtest_writel(q, CAPTURE_BASE + 4, 0x20);
    qtest_writel(q, CAPTURE_BASE + 8, 0x20);
    capture_group_result_packet(q, false);
    qtest_writel(q, CAPTURE_BASE + 0xc, 0x20);
    qtest_writel(q, CAPTURE_BASE + 0x14, 0x20);
    qtest_writel(q, CAPTURE_BASE, 0x20);
    qtest_writel(q, 0x2006bb44, 0x5a);
    qtest_clock_step(q, 1000000);
    capture_group_result_packet(q, false);
    for (unsigned port = 0; port < 2; port++) {
        qtest_system_reset(q);
        capture_group_result_packet(q, true);
        qtest_writel(q, CAPTURE_BASE + 0x100 + 4 * port, 0x12);
        capture_group_result_packet(q, false);
        qtest_writel(q, CAPTURE_BASE + 0x100 + 4 * port, 0);
        qtest_clock_step(q, 1000000);
        capture_group_result_packet(q, false);
    }
    qtest_system_reset(q);
    capture_group_result_packet(q, true);
    for (unsigned i = 0; i < ARRAY_SIZE(capture_group_results); i++) {
        uint32_t address = capture_group_results[i];
        g_autofree char *byte = qtest_hmp(q, "xp /1bx 0x%08x", address);
        g_autofree char *halfword = qtest_hmp(q, "xp /1hx 0x%08x", address);

        g_assert_nonnull(strstr(byte, "Cannot access memory"));
        g_assert_nonnull(strstr(halfword, "Cannot access memory"));
        endpoint_read_error(q, address + 1);
    }
    qtest_quit(q);
}

static void capture_group_cursor_configure(QTestState *q)
{
    static const uint32_t offsets[] = {
        0x33c, 0x344, 0x348, 0x380, 0x480, 0x580,
        0x350, 0x450, 0x550, 0x360, 0x460, 0x560,
    };
    static const uint32_t values[] = {
        1, 2, 3, 0x20, 0x30, 0x40,
        0x41000000, 0x42000000, 0x43000000,
        0x51000000, 0x52000000, 0x53000000,
    };

    for (unsigned i = 0; i < ARRAY_SIZE(offsets); i++) {
        qtest_writel(q, 0x2006b800 + offsets[i], values[i]);
    }
    qtest_writel(q, CAPTURE_BASE + 0x100, 0x10);
    qtest_writel(q, CAPTURE_BASE + 0x10, 0x20);
    qtest_writel(q, CAPTURE_BASE + 8, 0x20);
}

static void capture_group_cursor_packet(QTestState *q, const uint32_t *expected)
{
    for (unsigned i = 0; i < ARRAY_SIZE(capture_group_results); i++) {
        uint32_t address = capture_group_results[i];
        g_autofree char *reply = qtest_hmp(q, "xp /1wx 0x%08x", address);

        g_assert_null(strstr(reply, "Cannot access memory"));
        g_assert_cmphex(qtest_readl(q, address), ==, expected[i]);
    }
}

static void test_capture_group_cursor(void)
{
    static const uint32_t first[] = {
        1, 2, 0x41000040, 0x42000060, 0x43000080,
        0x51000040, 0x52000060, 0x53000080,
    };
    static const uint32_t next[] = {
        1, 3, 0x41000060, 0x42000090, 0x430000c0,
        0x51000060, 0x52000090, 0x530000c0,
    };
    static const uint32_t wrapped[] = {
        1, 0, 0x41000000, 0x42000000, 0x43000000,
        0x51000000, 0x52000000, 0x53000000,
    };
    static const uint32_t bank0[] = {
        0, 0, 0x41000000, 0x42000000, 0x43000000,
        0x51000000, 0x52000000, 0x53000000,
    };
    QTestState *q = capture_start(NULL);

    capture_group_cursor_configure(q);
    qtest_clock_step(q, 100000000);
    capture_group_result_packet(q, false); /* TG disabled. */
    qtest_writel(q, 0x20060070, 3); /* Forced high is not an automatic edge. */
    qtest_writel(q, 0x20060010, 1);
    qtest_clock_step(q, 100000000);
    capture_group_result_packet(q, false);
    qtest_writel(q, 0x20060070, 3);
    capture_group_result_packet(q, false);

    qtest_system_reset(q);
    capture_group_cursor_configure(q);
    qtest_writel(q, 0x20060010, 1);
    qtest_clock_step(q, 16666666);
    capture_group_result_packet(q, false);
    qtest_clock_step(q, 1);
    capture_group_cursor_packet(q, first);
    qtest_writel(q, 0x2006bb44, 0); /* Staging cannot echo into BB74. */
    qtest_writel(q, 0x2006bb50, 0xdeadbeef);
    qtest_writel(q, 0x2006bb80, 0x10);
    capture_group_cursor_packet(q, first);
    qtest_clock_step(q, 16666667); /* Falling edge. */
    capture_group_cursor_packet(q, first);
    qtest_clock_step(q, 16666667);
    capture_group_cursor_packet(q, next);
    qtest_clock_step(q, 33333334);
    capture_group_cursor_packet(q, wrapped);
    qtest_writel(q, 0x2006bb50, 0x41000000);
    qtest_writel(q, 0x2006bb80, 0x20);
    qtest_writel(q, 0x2006bb3c, 0);
    capture_group_cursor_packet(q, wrapped);
    qtest_writel(q, CAPTURE_BASE + 0x104, 0x10);
    capture_group_result_packet(q, false);
    qtest_clock_step(q, 33333334);
    capture_group_cursor_packet(q, bank0); /* New start0/bank0 after commit. */
    qtest_writel(q, 0x20060028, 1); /* Resync itself is not a cursor tick. */
    capture_group_cursor_packet(q, bank0);

    for (unsigned cancel = 0; cancel < 3; cancel++) {
        static const unsigned controls[] = { 0xc, 0x14, 0 };

        qtest_system_reset(q);
        capture_group_cursor_configure(q);
        qtest_writel(q, 0x20060010, 1);
        qtest_clock_step(q, 16666667);
        capture_group_cursor_packet(q, first);
        qtest_writel(q, CAPTURE_BASE + controls[cancel], 0x20);
        qtest_clock_step(q, 100000000);
        capture_group_result_packet(q, false);
    }

    for (unsigned invalid = 0; invalid < 6; invalid++) {
        static const uint32_t offsets[] = {
            0x33c, 0x344, 0x348, 0x480, 0x350, 0x560,
        };
        static const uint32_t values[] = {
            2, 4, 1, 0, 0xfffffff0, 0xfffffff0,
        };

        qtest_system_reset(q);
        capture_group_cursor_configure(q);
        qtest_writel(q, 0x2006b800 + offsets[invalid], values[invalid]);
        qtest_writel(q, CAPTURE_BASE + 0x104, 0x10);
        qtest_writel(q, 0x20060010, 1);
        qtest_clock_step(q, 100000000);
        capture_group_result_packet(q, false);
        /* Configuration retention remains independent of qualification. */
        g_assert_cmphex(qtest_readl(q, 0x2006b800 + offsets[invalid]), ==,
                       values[invalid]);
    }

    qtest_system_reset(q);
    qtest_writel(q, 0x2006bb44, 2); /* Missing the other required fields. */
    qtest_writel(q, CAPTURE_BASE + 0x100, 0x10);
    qtest_writel(q, CAPTURE_BASE + 0x10, 0x20);
    qtest_writel(q, CAPTURE_BASE + 8, 0x20);
    qtest_writel(q, 0x20060010, 1);
    qtest_clock_step(q, 100000000);
    capture_group_result_packet(q, false);

    for (unsigned i = 0; i < NIKON_CAPTURE_EVENTS; i++) {
        g_assert_cmphex(qtest_readl(q, CAPTURE_BASE + 0x210 + i * 0x20), ==, 0);
        g_assert_false(qtest_get_irq(q, i));
    }
    qtest_quit(q);
}

static void test_capture_group(void)
{
    static const struct {
        unsigned first, last, step;
        uint32_t mask;
    } fields[] = {
        { 0, 4, 4, 0xff }, { 8, 0x18, 4, 0xffff },
        { 0x1c, 0x1c, 4, 0xff }, { 0x20, 0x60, 0x20, 0xff },
        { 0x24, 0x3c, 4, 0x7f }, { 0x44, 0x5c, 4, 0x7f },
        { 0x64, 0x7c, 4, 0x7f }, { 0x80, 0x180, 4, 0xffff },
        { 0x184, 0x184, 4, UINT32_MAX }, { 0x200, 0x20c, 4, 0xffff },
        { 0x210, 0x210, 4, UINT32_MAX }, { 0x214, 0x228, 4, 0xff },
        { 0x22c, 0x22c, 4, UINT32_MAX }, { 0x230, 0x230, 4, 1 },
        { 0x234, 0x250, 4, 0xffff }, { 0x294, 0x294, 4, 0xffff },
        { 0x298, 0x29c, 4, 0x7fffff }, { 0x2a0, 0x2a0, 4, UINT32_MAX },
        { 0x2a4, 0x2ac, 4, 0xffff }, { 0x2b0, 0x2b8, 4, 0xff },
        { 0x318, 0x318, 4, 0xff }, { 0x324, 0x324, 4, 0xff },
        { 0x33c, 0x33c, 4, UINT32_MAX }, { 0x344, 0x34c, 4, 0xff },
        { 0x350, 0x550, 0x100, UINT32_MAX },
        { 0x360, 0x560, 0x100, UINT32_MAX },
        { 0x380, 0x580, 0x100, UINT32_MAX },
    };
    static const uint32_t patterns[] = {
        UINT32_MAX, 0xa55a3cc3, 0x80000000, 0,
    };
    const uint32_t base = 0x2006b800;
    uint32_t expected[0x584 / 4] = { 0 };
    bool known[ARRAY_SIZE(expected)] = { false };
    unsigned count = 0;
    QTestState *q = capture_start(NULL);

    qtest_writel(q, base, 0xff);
    g_assert_cmphex(qtest_readl(q, base), ==, 0xff);
    qtest_memset(q, RAM + 0x1000, 0xa7, 64);
    for (unsigned i = 0; i < 8; i++) {
        qtest_writel(q, CAPTURE_BASE + 0x200 + i * 0x20, UINT32_MAX);
    }
    for (unsigned pass = 0; pass < ARRAY_SIZE(patterns); pass++) {
        for (unsigned i = 0; i < ARRAY_SIZE(fields); i++) {
            for (unsigned offset = fields[i].first; offset <= fields[i].last;
                 offset += fields[i].step) {
                unsigned word = offset / 4;
                uint32_t value = (patterns[pass] ^ (offset * 0x01010101)) &
                                 fields[i].mask;

                if (!pass) {
                    g_assert_false(known[word]);
                    known[word] = true;
                    count++;
                }
                expected[word] = value;
                qtest_writel(q, base + offset, value);
                g_assert_cmphex(qtest_readl(q, base + offset), ==, value);
                for (unsigned bit = 0; !pass && bit < 32; bit++) {
                    if (!(fields[i].mask & (1U << bit))) {
                        qtest_writel(q, base + offset, value | (1U << bit));
                        g_assert_cmphex(qtest_readl(q, base + offset), ==, value);
                    }
                }
            }
        }
        for (unsigned word = 0; word < ARRAY_SIZE(expected); word++) {
            if (known[word]) {
                g_assert_cmphex(qtest_readl(q, base + word * 4), ==,
                                expected[word]);
            }
        }
    }
    g_assert_cmpuint(count, ==, 144);
    for (unsigned i = 0; i < ARRAY_SIZE(capture_group_results); i++) {
        known[(capture_group_results[i] - base) / 4] = true;
    }
    capture_group_result_packet(q, true);
    for (unsigned i = 0; i < 8; i++) {
        expected[0x234 / 4 + i] = 0x1200 + i;
        qtest_writel(q, base + 0x234 + i * 4, expected[0x234 / 4 + i]);
    }
    qtest_writel(q, base + 0x230, 1);
    qtest_writel(q, base + 0x230, 0);
    for (unsigned i = 0; i < 8; i++) {
        g_assert_cmphex(qtest_readl(q, base + 0x234 + i * 4), ==, 0x1200 + i);
    }
    const uint32_t unsupported_masks[] = { 2, 3, 7 };

    for (unsigned i = 0; i < ARRAY_SIZE(unsupported_masks); i++) {
        qtest_writel(q, base + 0x230, unsupported_masks[i]);
        g_assert_cmphex(qtest_readl(q, base + 0x230), ==, 0);
    }
    /* Opaque plane addresses are retained independently, without DMA. */
    for (unsigned bank = 0; bank < 3; bank++) {
        const unsigned offsets[] = { 0x350, 0x360, 0x380 };

        for (unsigned i = 0; i < ARRAY_SIZE(offsets); i++) {
            unsigned offset = offsets[i] + bank * 0x100;
            uint32_t value = RAM + 0x1000 + bank * 16 + i * 4;

            qtest_writel(q, base + offset, value);
            g_assert_cmphex(qtest_readl(q, base + offset), ==, value);
        }
    }
    for (unsigned word = 0; word < ARRAY_SIZE(known); word++) {
        if (!known[word]) {
            qtest_writel(q, base + word * 4, UINT32_MAX);
            endpoint_read_error(q, base + word * 4);
        }
    }
    qtest_writeb(q, base, 0x5a);
    qtest_writew(q, base, 0xa5);
    qtest_writel(q, base + 1, UINT32_MAX);
    g_assert_cmphex(qtest_readl(q, base), ==, expected[0]);
    for (unsigned i = 0; i < 2; i++) {
        g_autofree char *reply = qtest_hmp(q, "xp /1%cx 0x%08x",
                                           i ? 'h' : 'b', base);

        g_assert_nonnull(strstr(reply, "Cannot access memory"));
    }
    endpoint_read_error(q, base + 1);
    endpoint_read_error(q, base - 4);
    endpoint_read_error(q, base + 0x584);
    qtest_clock_step(q, 1000000);
    g_assert_cmphex(qtest_readl(q, CAPTURE_BASE + 8), ==, 0);
    for (unsigned i = 0; i < 8; i++) {
        g_assert_cmphex(qtest_readl(q, CAPTURE_BASE + 0x210 + i * 0x20), ==, 0);
    }
    for (unsigned i = 0; i < NIKON_CAPTURE_EVENTS; i++) {
        g_assert_false(qtest_get_irq(q, i));
    }
    g_assert_cmpuint(property_at(q, "/machine/readout-control",
                                 "request-generation"), ==, 0);
    for (unsigned offset = 0; offset < 64; offset += 4) {
        g_assert_cmphex(qtest_readl(q, RAM + 0x1000 + offset), ==, 0xa7a7a7a7);
    }
    qtest_system_reset(q);
    for (unsigned word = 0; word < ARRAY_SIZE(known); word++) {
        if (known[word]) {
            g_assert_cmphex(qtest_readl(q, base + word * 4), ==, 0);
        } else {
            endpoint_read_error(q, base + word * 4);
        }
    }
    qtest_clock_step(q, 1000000);
    for (unsigned i = 0; i < NIKON_CAPTURE_EVENTS; i++) {
        g_assert_false(qtest_get_irq(q, i));
    }
    qtest_quit(q);
}

static void test_capture_endpoints(void)
{
    static const struct {
        unsigned offset;
        uint32_t mask;
    } fields[] = {
        { 0x14, UINT32_MAX }, { 0x18, 0xff }, { 0x24, 0xff }, { 0x3c, 7 },
        { 0x40, UINT32_MAX }, { 0x44, 0xff }, { 0x48, 0xff }, { 0x4c, 0xff },
        { 0x50, UINT32_MAX }, { 0x54, UINT32_MAX }, { 0x58, UINT32_MAX },
        { 0x5c, UINT32_MAX }, { 0x60, UINT32_MAX }, { 0x64, UINT32_MAX },
        { 0x68, UINT32_MAX }, { 0x6c, UINT32_MAX }, { 0x80, UINT32_MAX },
    };
    static const uint32_t patterns[] = {
        UINT32_MAX, 0xa55a3cc3, 0x01020304, 0x80000000, 0,
    };
    const uint32_t base = 0x2006a400;
    uint32_t expected[10][ARRAY_SIZE(fields)] = { 0 };
    QTestState *q = capture_start(NULL);

    /* The full-word constructor is not a boolean or a whitelist for value3. */
    qtest_writel(q, base + 0x14, 0x80000003);
    g_assert_cmphex(qtest_readl(q, base + 0x14), ==, 0x80000003);
    qtest_memset(q, RAM + 0x1000, 0xa7, 32);
    qtest_writel(q, CAPTURE_BASE + 0x200, UINT32_MAX);
    qtest_writel(q, CAPTURE_BASE + 0x220, UINT32_MAX);
    for (unsigned pass = 0; pass < ARRAY_SIZE(patterns); pass++) {
        for (unsigned endpoint = 0; endpoint < 10; endpoint++) {
            bool extended = endpoint == 2 || endpoint == 3 ||
                            endpoint == 4 || endpoint == 9;

            for (unsigned i = 0; i < ARRAY_SIZE(fields); i++) {
                unsigned offset = fields[i].offset;
                uint32_t address = base + endpoint * 0x100 + offset;
                uint32_t value = patterns[pass] ^ (endpoint * 0x01010000 + offset);
                bool sparse = (offset >= 0x54 && offset <= 0x5c) ||
                              (offset >= 0x64 && offset <= 0x6c);

                if (sparse && !extended) {
                    if (!pass) {
                        qtest_writel(q, address, value);
                        endpoint_read_error(q, address);
                    }
                    continue;
                }
                value &= fields[i].mask;
                expected[endpoint][i] = value;
                qtest_writel(q, address, value);
                g_assert_cmphex(qtest_readl(q, address), ==, value);
                for (unsigned bit = 0; !pass && bit < 32; bit++) {
                    if (!(fields[i].mask & (1U << bit))) {
                        qtest_writel(q, address, value | (1U << bit));
                        g_assert_cmphex(qtest_readl(q, address), ==, value);
                    }
                }
            }
        }
        /* Later endpoints cannot alias earlier retained configurations. */
        for (unsigned endpoint = 0; endpoint < 10; endpoint++) {
            bool extended = endpoint == 2 || endpoint == 3 ||
                            endpoint == 4 || endpoint == 9;

            for (unsigned i = 0; i < ARRAY_SIZE(fields); i++) {
                unsigned offset = fields[i].offset;

                if (!extended && ((offset >= 0x54 && offset <= 0x5c) ||
                                  (offset >= 0x64 && offset <= 0x6c))) {
                    continue;
                }
                g_assert_cmphex(qtest_readl(q, base + endpoint * 0x100 + offset),
                                ==, expected[endpoint][i]);
            }
        }
    }
    for (unsigned endpoint = 0; endpoint < 10; endpoint++) {
        uint32_t address = base + endpoint * 0x100;

        /* Reproduce the independent selector bits and the separate low bit. */
        qtest_writel(q, address + 0x3c, 1);
        for (unsigned selector = 0; selector < 4; selector++) {
            uint32_t value = (qtest_readl(q, address + 0x3c) & ~6U) |
                             (selector << 1);

            qtest_writel(q, address + 0x3c, value);
            g_assert_cmphex(qtest_readl(q, address + 0x3c), ==,
                            1 | (selector << 1));
            qtest_writel(q, address + 0x3c, value & ~1U);
            g_assert_cmphex(qtest_readl(q, address + 0x3c), ==, selector << 1);
            qtest_writel(q, address + 0x3c, value);
        }
        qtest_writeb(q, address + 0x14, 0xff);
        qtest_writew(q, address + 0x14, 0xffff);
        qtest_writel(q, address + 0x15, UINT32_MAX);
        g_assert_cmphex(qtest_readl(q, address + 0x14), ==, expected[endpoint][0]);
        qtest_writel(q, address + 0x1c, UINT32_MAX);
        endpoint_read_error(q, address + 0x1c);
        for (unsigned status = 0x70; status <= 0x7c; status += 4) {
            qtest_writel(q, address + status, UINT32_MAX);
            endpoint_read_error(q, address + status);
        }
    }
    endpoint_read_error(q, base - 4);
    endpoint_read_error(q, base + 0x984);
    endpoint_read_error(q, base + 10 * 0x100 + 0x14);
    qtest_clock_step(q, 1000000);
    for (unsigned i = 0; i < 8; i++) {
        g_assert_cmphex(qtest_readl(q, CAPTURE_BASE + 0x210 + i * 0x20), ==, 0);
    }
    for (unsigned i = 0; i < NIKON_CAPTURE_EVENTS; i++) {
        g_assert_false(qtest_get_irq(q, i));
    }
    g_assert_cmpuint(property_at(q, "/machine/readout-control",
                                 "request-generation"), ==, 0);
    for (unsigned offset = 0; offset < 32; offset += 4) {
        g_assert_cmphex(qtest_readl(q, RAM + 0x1000 + offset), ==, 0xa7a7a7a7);
    }
    qtest_system_reset(q);
    for (unsigned endpoint = 0; endpoint < 10; endpoint++) {
        bool extended = endpoint == 2 || endpoint == 3 ||
                        endpoint == 4 || endpoint == 9;

        for (unsigned i = 0; i < ARRAY_SIZE(fields); i++) {
            unsigned offset = fields[i].offset;

            if (!extended && ((offset >= 0x54 && offset <= 0x5c) ||
                              (offset >= 0x64 && offset <= 0x6c))) {
                continue;
            }
            g_assert_cmphex(qtest_readl(q, base + endpoint * 0x100 + offset),
                            ==, 0);
        }
        endpoint_read_error(q, base + endpoint * 0x100 + 0x70);
    }
    qtest_clock_step(q, 1000000);
    for (unsigned i = 0; i < NIKON_CAPTURE_EVENTS; i++) {
        g_assert_false(qtest_get_irq(q, i));
    }
    qtest_quit(q);
}

static void test_capture_endpoint_globals(void)
{
    static const struct {
        unsigned first, last, step;
        uint32_t mask;
    } fields[] = {
        { 0x200, 0x200, 4, 0xffff00ff }, { 0x204, 0x204, 4, UINT32_MAX },
        { 0x208, 0x210, 4, 0xffff }, { 0x214, 0x21c, 4, 0x0fffffff },
        { 0x220, 0x220, 4, 0xffff }, { 0x224, 0x258, 4, 0x0fffffff },
        { 0x25c, 0x25c, 4, 0xffffff }, { 0x260, 0x26c, 4, 0xffff },
        { 0x270, 0x274, 4, 0x0fffffff }, { 0x278, 0x278, 4, 0xffff },
        { 0x27c, 0x27c, 4, UINT32_MAX }, { 0x280, 0x280, 4, 0xff },
        { 0x284, 0x284, 4, 0x03ffffff }, { 0x288, 0x288, 4, 0xffff },
        { 0x28c, 0x28c, 4, 0x7fffffff }, { 0x290, 0x290, 4, 0xffff },
        { 0x294, 0x2a0, 4, 0x03ffffff }, { 0x2a4, 0x2a4, 4, 0xffff },
        { 0x2a8, 0x2b4, 4, 0x03ffffff }, { 0x2b8, 0x2b8, 4, 0xffff },
        { 0x2bc, 0x304, 0x18, UINT32_MAX },
        { 0x2c0, 0x308, 0x18, 0xffffff },
        { 0x2c4, 0x30c, 0x18, 0xffff },
        { 0x2c8, 0x310, 0x18, 0xffff },
        { 0x2cc, 0x314, 0x18, 0x03ffffff },
        { 0x2d0, 0x318, 0x18, 0xffff }, { 0x31c, 0x31c, 4, 0xff },
        { 0x320, 0x358, 0x1c, UINT32_MAX },
        { 0x324, 0x35c, 0x1c, 0xffff },
        { 0x328, 0x360, 0x1c, 0xffff },
        { 0x32c, 0x364, 0x1c, 0x03ffffff },
        { 0x330, 0x368, 0x1c, 0xffff },
        { 0x334, 0x36c, 0x1c, 0xffff },
        { 0x338, 0x338, 4, 0x03ffffff },
        { 0x354, 0x354, 4, 0x07ffffff },
        { 0x370, 0x370, 4, 0x03ffffff },
    };
    static const uint32_t patterns[] = { UINT32_MAX, 0xa55a3cc3, 0x80000000, 0 };
    const uint32_t base = 0x2006a000;
    uint32_t expected[0x174 / 4];
    bool covered[ARRAY_SIZE(expected)] = { false };
    QTestState *q = capture_start(NULL);

    qtest_memset(q, RAM + 0x1000, 0xa7, 32);
    for (unsigned pass = 0; pass < ARRAY_SIZE(patterns); pass++) {
        for (unsigned i = 0; i < ARRAY_SIZE(fields); i++) {
            for (unsigned offset = fields[i].first; offset <= fields[i].last;
                 offset += fields[i].step) {
                unsigned word = (offset - 0x200) / 4;
                uint32_t value = (patterns[pass] ^ (offset * 0x01010101)) &
                                 fields[i].mask;

                if (!pass) {
                    g_assert_false(covered[word]);
                    covered[word] = true;
                    g_assert_cmphex(qtest_readl(q, base + offset), ==, 0);
                }
                expected[word] = value;
                qtest_writel(q, base + offset, value);
                g_assert_cmphex(qtest_readl(q, base + offset), ==, value);
                for (unsigned bit = 0; !pass && bit < 32; bit++) {
                    if (!(fields[i].mask & (1U << bit))) {
                        qtest_writel(q, base + offset, value | (1U << bit));
                        g_assert_cmphex(qtest_readl(q, base + offset), ==, value);
                    }
                }
            }
        }
        for (unsigned word = 0; word < ARRAY_SIZE(expected); word++) {
            g_assert_true(covered[word]);
            g_assert_cmphex(qtest_readl(q, base + 0x200 + word * 4), ==,
                            expected[word]);
        }
    }
    /* AF18 skips remaining stores when204 is zero; it does not clear them. */
    qtest_writel(q, base + 0x204, 0);
    expected[1] = 0;
    for (unsigned word = 0; word < ARRAY_SIZE(expected); word++) {
        g_assert_cmphex(qtest_readl(q, base + 0x200 + word * 4), ==,
                        expected[word]);
    }
    qtest_writeb(q, base + 0x200, 0xff);
    qtest_writew(q, base + 0x200, 0xffff);
    qtest_writel(q, base + 0x201, UINT32_MAX);
    g_assert_cmphex(qtest_readl(q, base + 0x200), ==, expected[0]);
    for (unsigned offset = 0x374; offset < 0x400; offset += 4) {
        qtest_writel(q, base + offset, UINT32_MAX);
        endpoint_read_error(q, base + offset);
    }
    endpoint_read_error(q, base + 0x1fc);
    qtest_clock_step(q, 1000000);
    for (unsigned i = 0; i < NIKON_CAPTURE_EVENTS; i++) {
        g_assert_false(qtest_get_irq(q, i));
    }
    g_assert_cmpuint(property_at(q, "/machine/readout-control",
                                 "request-generation"), ==, 0);
    for (unsigned offset = 0; offset < 32; offset += 4) {
        g_assert_cmphex(qtest_readl(q, RAM + 0x1000 + offset), ==, 0xa7a7a7a7);
    }
    qtest_system_reset(q);
    for (unsigned word = 0; word < ARRAY_SIZE(expected); word++) {
        g_assert_cmphex(qtest_readl(q, base + 0x200 + word * 4), ==, 0);
    }
    endpoint_read_error(q, base + 0x3e0);
    qtest_quit(q);
}

static void test_capture_controls(void)
{
    QTestState *q = capture_start(NULL);
    uint32_t selected = 0x2bf;

    /* 10239740 complements 102396e8's selected units before initialization. */
    qtest_writel(q, CAPTURE_BASE, ~selected);
    g_assert_cmphex(qtest_readl(q, CAPTURE_BASE), ==, (~selected & 0xfff));
    qtest_writel(q, CAPTURE_BASE + 4, 0xfff);
    for (unsigned bit = 0; bit < 12; bit++) {
        uint32_t unit = 1U << bit;

        qtest_writel(q, CAPTURE_BASE, unit);
        qtest_writel(q, CAPTURE_BASE, 0);
        g_assert_cmphex(qtest_readl(q, CAPTURE_BASE + 4), ==, unit);
        qtest_writel(q, CAPTURE_BASE + 4, unit);
        g_assert_cmphex(qtest_readl(q, CAPTURE_BASE), ==, 0);
        qtest_writel(q, CAPTURE_BASE + 0x10, 0xfff);
        qtest_writel(q, CAPTURE_BASE + 0x14, unit);
        g_assert_cmphex(qtest_readl(q, CAPTURE_BASE + 0x10), ==,
                        (0xfff & ~unit));
        qtest_writel(q, CAPTURE_BASE + 0x10, unit);
        g_assert_cmphex(qtest_readl(q, CAPTURE_BASE + 0x14), ==, 0xfff);
    }
    qtest_writel(q, CAPTURE_BASE + 0x18, selected);
    qtest_writel(q, CAPTURE_BASE + 0x18,
                 qtest_readl(q, CAPTURE_BASE + 0x18) & ~0x20);
    g_assert_cmphex(qtest_readl(q, CAPTURE_BASE + 0x18), ==,
                    (selected & ~0x20));
    qtest_writel(q, CAPTURE_BASE + 0x24, 15);
    qtest_writel(q, CAPTURE_BASE + 0x20, 31);
    qtest_writel(q, CAPTURE_BASE + 0x28, 1);
    qtest_writel(q, CAPTURE_BASE + 0x60, 0x80000001);
    qtest_writel(q, CAPTURE_BASE + 0x70, (3 << 3) | (1 << 5) | (1 << 1));
    g_assert_cmphex(qtest_readl(q, CAPTURE_BASE + 0x24), ==, 15);
    g_assert_cmphex(qtest_readl(q, CAPTURE_BASE + 0x20), ==, 31);
    g_assert_cmphex(qtest_readl(q, CAPTURE_BASE + 0x28), ==, 1);
    g_assert_cmphex(qtest_readl(q, CAPTURE_BASE + 0x60), ==, 0x80000001);
    g_assert_cmphex(qtest_readl(q, CAPTURE_BASE + 0x70), ==, 0x3a);
    qtest_writel(q, CAPTURE_BASE, 0x1000); /* Neither mask nor complement. */
    qtest_writew(q, CAPTURE_BASE + 0x10, 0);
    qtest_writel(q, CAPTURE_BASE + 0x18, 0x1000);
    qtest_writel(q, CAPTURE_BASE + 0x28, 2);
    qtest_writel(q, CAPTURE_BASE + 0x70, 1);
    g_assert_cmphex(qtest_readl(q, CAPTURE_BASE), ==, 0);
    g_assert_cmphex(qtest_readl(q, CAPTURE_BASE + 0x10), ==, 0xfff);
    g_assert_cmphex(qtest_readl(q, CAPTURE_BASE + 0x18), ==,
                    (selected & ~0x20));
    g_assert_cmphex(qtest_readl(q, CAPTURE_BASE + 0x28), ==, 1);
    g_assert_cmphex(qtest_readl(q, CAPTURE_BASE + 0x70), ==, 0x3a);
    for (unsigned i = 0; i < NIKON_CAPTURE_EVENTS; i++) {
        g_assert_false(qtest_get_irq(q, i));
    }
    qtest_writel(q, CAPTURE_BASE + 0x200, 8);
    status_word(q, CAPTURE_PATH, 0, 8);
    status_word(q, CAPTURE_PATH, 1, 0);
    publish_capture(q, 0);
    g_assert_true(qtest_get_irq(q, 0));
    qtest_writel(q, CAPTURE_BASE, UINT32_MAX);
    qtest_writel(q, CAPTURE_BASE + 0x14, 0xfff);
    g_assert_cmphex(qtest_readl(q, CAPTURE_BASE + 0x210), ==, 8);
    g_assert_true(qtest_get_irq(q, 0));
    qtest_qmp_assert_success(q, "{'execute':'system_reset'}");
    qtest_qmp_eventwait(q, "RESET");
    for (unsigned offset = 0; offset <= 0x28; offset += 4) {
        if (offset != 0x1c) {
            g_assert_cmphex(qtest_readl(q, CAPTURE_BASE + offset), ==, 0);
        }
    }
    g_assert_cmphex(qtest_readl(q, CAPTURE_BASE + 0x60), ==, 0);
    g_assert_cmphex(qtest_readl(q, CAPTURE_BASE + 0x70), ==, 0);
    g_assert_false(qtest_get_irq(q, 0));
    qtest_quit(q);
}

static void test_capture_runstop(void)
{
    static const struct {
        uint32_t unit, first, second, last;
    } stops[] = {
        { 1, 0x200, 0, 0 }, { 2, 0, 0, 0x38 }, { 4, 0x8000, 0, 0 },
        { 8, 0x8000, 0, 0 }, { 0x10, 0x80000000, 1, 0 },
        { 0x20, 0x18000000, 0, 0 }, { 0x40, 0x20000, 0, 0 },
        { 0x80, 0x80000, 0, 0 }, { 0x200, 0, 0x180, 0 },
        { 0x400, 0, 0x18, 0 }, { 0x800, 0, 0x1800, 0 },
    };
    QTestState *q = capture_start(NULL);

    qtest_writel(q, CAPTURE_BASE + 0x200, UINT32_MAX);
    qtest_writel(q, CAPTURE_BASE + 0x220, UINT32_MAX);
    qtest_writel(q, CAPTURE_BASE + 0x2e0, UINT32_MAX);
    qtest_writel(q, CAPTURE_BASE + 0x10, 0xeff);
    for (unsigned i = 0; i < ARRAY_SIZE(stops); i++) {
        qtest_writel(q, CAPTURE_BASE + 8, stops[i].unit);
        g_assert_cmphex(qtest_readl(q, CAPTURE_BASE + 8), ==, stops[i].unit);
        qtest_clock_step(q, 2000);
        g_assert_cmphex(qtest_readl(q, CAPTURE_BASE + 0x210), ==, 0);
        qtest_writel(q, CAPTURE_BASE + 0xc, stops[i].unit);
        g_assert_cmphex(qtest_readl(q, CAPTURE_BASE + 8), ==, 0);
        qtest_clock_step(q, 999);
        g_assert_cmphex(qtest_readl(q, CAPTURE_BASE + 0x210), ==, 0);
        g_assert_cmphex(qtest_readl(q, CAPTURE_BASE + 0x230), ==, 0);
        qtest_clock_step(q, 1);
        g_assert_cmphex(qtest_readl(q, CAPTURE_BASE + 0x210), ==,
                        stops[i].first);
        g_assert_cmphex(qtest_readl(q, CAPTURE_BASE + 0x230), ==,
                        stops[i].second);
        g_assert_cmphex(qtest_readl(q, CAPTURE_BASE + 0x2f0), ==,
                        stops[i].last);
        g_assert_cmpint(qtest_get_irq(q, 4), ==, !!stops[i].last);
        g_assert_cmpint(qtest_get_irq(q, 0), ==,
                        !!(stops[i].first | stops[i].second));
        qtest_writel(q, CAPTURE_BASE + 0x210, UINT32_MAX);
        qtest_writel(q, CAPTURE_BASE + 0x230, UINT32_MAX);
        qtest_writel(q, CAPTURE_BASE + 0x2f0, UINT32_MAX);
    }

    /* Quiescence is idempotent; independent units keep separate deadlines. */
    qtest_writel(q, CAPTURE_BASE + 0x200, 0);
    qtest_writel(q, CAPTURE_BASE + 0xc, 1);
    qtest_clock_step(q, 999);
    qtest_writel(q, CAPTURE_BASE + 0xc, 0x401);
    qtest_clock_step(q, 1);
    g_assert_cmphex(qtest_readl(q, CAPTURE_BASE + 0x210), ==, 0x200);
    g_assert_cmphex(qtest_readl(q, CAPTURE_BASE + 0x230), ==, 0);
    g_assert_false(qtest_get_irq(q, 0));
    qtest_writel(q, CAPTURE_BASE + 0x200, 0x200);
    g_assert_true(qtest_get_irq(q, 0));
    qtest_writel(q, CAPTURE_BASE + 0x210, 0x200);
    qtest_clock_step(q, 999);
    g_assert_cmphex(qtest_readl(q, CAPTURE_BASE + 0x230), ==, 0x18);
    qtest_writel(q, CAPTURE_BASE + 0x230, 0x18);

    /* The4/8 drain consumer is shared: one live half prevents notification. */
    qtest_writel(q, CAPTURE_BASE + 8, 0xc);
    qtest_writel(q, CAPTURE_BASE + 0xc, 4);
    qtest_clock_step(q, 1000);
    g_assert_cmphex(qtest_readl(q, CAPTURE_BASE + 0x210), ==, 0);
    g_assert_cmphex(qtest_readl(q, CAPTURE_BASE + 8), ==, 8);
    qtest_writel(q, CAPTURE_BASE + 0xc, 8);
    qtest_clock_step(q, 1000);
    g_assert_cmphex(qtest_readl(q, CAPTURE_BASE + 0x210), ==, 0x8000);
    qtest_writel(q, CAPTURE_BASE + 0x210, 0x8000);

    qtest_writel(q, CAPTURE_BASE + 8, 0xc);
    qtest_writel(q, CAPTURE_BASE + 0xc, 4);
    qtest_clock_step(q, 999);
    qtest_writel(q, CAPTURE_BASE + 0xc, 8);
    qtest_clock_step(q, 1);
    g_assert_cmphex(qtest_readl(q, CAPTURE_BASE + 0x210), ==, 0);
    qtest_clock_step(q, 999);
    g_assert_cmphex(qtest_readl(q, CAPTURE_BASE + 0x210), ==, 0x8000);
    qtest_writel(q, CAPTURE_BASE + 0x210, 0x8000);
    qtest_writel(q, CAPTURE_BASE + 0xc, 4);
    qtest_writel(q, CAPTURE_BASE, 8);
    qtest_clock_step(q, 1000);
    g_assert_cmphex(qtest_readl(q, CAPTURE_BASE + 0x210), ==, 0x8000);
    qtest_writel(q, CAPTURE_BASE + 0x210, 0x8000);
    qtest_writel(q, CAPTURE_BASE + 4, 8);

    /* An expired half retains its identity if the other half is cancelled. */
    qtest_writel(q, CAPTURE_BASE + 0xc, 4);
    qtest_clock_step(q, 999);
    qtest_writel(q, CAPTURE_BASE + 0xc, 8);
    qtest_clock_step(q, 501);
    g_assert_cmphex(qtest_readl(q, CAPTURE_BASE + 0x210), ==, 0);
    qtest_writel(q, CAPTURE_BASE, 8);
    g_assert_cmphex(qtest_readl(q, CAPTURE_BASE + 0x210), ==, 0x8000);
    qtest_writel(q, CAPTURE_BASE + 0x210, 0x8000);
    qtest_clock_step(q, 1000);
    g_assert_cmphex(qtest_readl(q, CAPTURE_BASE + 0x210), ==, 0);
    qtest_writel(q, CAPTURE_BASE + 4, 8);

    /* Start supersedes an uncompleted stop; reset/disable cancel per unit. */
    qtest_writel(q, CAPTURE_BASE + 0xc, 0xeff);
    qtest_writel(q, CAPTURE_BASE + 8, 3);
    qtest_writel(q, CAPTURE_BASE, 0x2fc);
    qtest_writel(q, CAPTURE_BASE + 0x14, 0xc00);
    qtest_clock_step(q, 2000);
    g_assert_cmphex(qtest_readl(q, CAPTURE_BASE + 0x210), ==, 0);
    g_assert_cmphex(qtest_readl(q, CAPTURE_BASE + 0x230), ==, 0);
    g_assert_cmphex(qtest_readl(q, CAPTURE_BASE + 8), ==, 3);
    /* Disabled/reset units stay idle. */
    qtest_writel(q, CAPTURE_BASE + 8, 0xeff);
    g_assert_cmphex(qtest_readl(q, CAPTURE_BASE + 8), ==, 3);
    qtest_writel(q, CAPTURE_BASE + 8, 0x100);
    qtest_writel(q, CAPTURE_BASE + 0xc, 0x1000);
    qtest_writel(q, CAPTURE_BASE + 0xc, 0xffffffff);
    qtest_writeb(q, CAPTURE_BASE + 0xc, 1);
    qtest_writew(q, CAPTURE_BASE + 0xc, 1);
    qtest_writel(q, CAPTURE_BASE + 0xd, 1);
    qtest_clock_step(q, 2000);
    g_assert_cmphex(qtest_readl(q, CAPTURE_BASE + 8), ==, 3);
    g_assert_cmphex(qtest_readl(q, CAPTURE_BASE + 0x210), ==, 0);
    qtest_writel(q, CAPTURE_BASE + 0xc, 0xeff);
    qtest_system_reset(q);
    qtest_clock_step(q, 2000);
    g_assert_cmphex(qtest_readl(q, CAPTURE_BASE + 8), ==, 0);
    g_assert_cmphex(qtest_readl(q, CAPTURE_BASE + 0x210), ==, 0);
    g_assert_cmphex(qtest_readl(q, CAPTURE_BASE + 0x230), ==, 0);
    qtest_writel(q, CAPTURE_BASE + 0xc, 1);
    qtest_qmp_assert_success(q, "{'execute':'qom-set','arguments':{"
                            "'path':%s,'property':'realized','value':false}}",
                            CAPTURE_PATH);
    qtest_clock_step(q, 2000);
    g_assert_cmphex(qtest_readl(q, CAPTURE_BASE + 0x210), ==, 0);
    qtest_quit(q);
}

static void test_capture_status(void)
{
    QTestState *q = capture_start(NULL);
    static const unsigned events[] = { 0, 0, 1, 2, 2, 3, 3, 4 };
    unsigned i;

    for (i = 0; i < ARRAY_SIZE(events); i++) {
        status_word(q, CAPTURE_PATH, i, 1U << i);
    }
    for (i = 0; i < NIKON_CAPTURE_EVENTS; i++) {
        publish_capture(q, i);
        g_assert_false(qtest_get_irq(q, i));
    }
    /* Complete events latch before masks are programmed. Enabling their
     * actual pending bits makes the corresponding interrupt observable.
     */
    for (i = 0; i < ARRAY_SIZE(events); i++) {
        uint64_t mask = CAPTURE_BASE + 0x200 + i * 0x20;
        uint64_t status = mask + 0x10;

        g_assert_cmphex(qtest_readl(q, status), ==, 1U << i);
        qtest_writel(q, mask, 1U << i);
        g_assert_true(qtest_get_irq(q, events[i]));
        qtest_writel(q, mask, 0);
        g_assert_false(qtest_get_irq(q, events[i]));
    }
    for (i = 0; i < ARRAY_SIZE(events); i++) {
        qtest_writel(q, CAPTURE_BASE + 0x200 + i * 0x20, UINT32_MAX);
    }
    qtest_writel(q, CAPTURE_BASE + 0x210, 1);
    g_assert_true(qtest_get_irq(q, 0));
    qtest_writel(q, CAPTURE_BASE + 0x230, 2);
    g_assert_false(qtest_get_irq(q, 0));
    for (i = 2; i < ARRAY_SIZE(events); i++) {
        qtest_writel(q, CAPTURE_BASE + 0x210 + i * 0x20, 1U << i);
    }
    for (i = 0; i < NIKON_CAPTURE_EVENTS; i++) {
        g_assert_false(qtest_get_irq(q, i));
    }
    qtest_quit(q);
}

static void test_capture_publication(void)
{
    QTestState *q = capture_start(NULL);

    qtest_writel(q, CAPTURE_BASE + 0x2a0, UINT32_MAX);
    qtest_writel(q, CAPTURE_BASE + 0x2c0, UINT32_MAX);
    status_word(q, CAPTURE_PATH, 5, 0x40);
    publish_capture(q, 3);
    g_assert_false(qtest_get_irq(q, 3));
    g_assert_cmphex(qtest_readl(q, CAPTURE_BASE + 0x2b0), ==, 0);
    status_word(q, CAPTURE_PATH, 6, 0);
    publish_capture(q, 3);
    g_assert_true(qtest_get_irq(q, 3));
    qtest_writel(q, CAPTURE_BASE + 0x2b0, 0x40);
    publish_capture(q, 3);
    g_assert_false(qtest_get_irq(q, 3));
    status_word(q, CAPTURE_PATH, 5, 0x40);
    status_word(q, CAPTURE_PATH, 6, 0);
    publish_capture(q, 3);
    qtest_qmp_assert_success(q, "{'execute':'system_reset'}");
    qtest_qmp_eventwait(q, "RESET");
    g_assert_false(qtest_get_irq(q, 3));
    g_assert_cmphex(qtest_readl(q, CAPTURE_BASE + 0x2b0), ==, 0);
    g_assert_cmphex(qtest_readl(q, CAPTURE_BASE + 0x2a0), ==, 0);
    qtest_writel(q, CAPTURE_BASE + 0x2a0, UINT32_MAX);
    publish_capture(q, 3);
    g_assert_false(qtest_get_irq(q, 3));
    qtest_quit(q);
}

#define READOUT_PATH "/machine/readout-control"
#define READOUT_BUFFERS 0x20069800
#define READOUT_PARAMETERS 0x2006b000

static QTestState *readout_start(void)
{
    g_autofree char *bios = g_shell_quote(image);
    QTestState *q = qtest_initf(
        "-M z7ii-mas-diagnostic -smp 1 -bios %s -serial none", bios);

    qtest_irq_intercept_out_named(q, READOUT_PATH, "readout-request");
    return q;
}

static void test_readout_configuration(void)
{
    QTestState *q = readout_start();
    static const struct {
        unsigned offset;
        uint32_t value;
    } fields[] = {
        { 0x310, 0xfedcba98 }, { 0x318, 0x80000001 },
        { 0x324, 0x005a00a5 }, { 0x334, 0xffff }, { 0x354, 0xff },
        { 0x364, 0xffff00ff }, { 0x36c, 0xabcdef },
        { 0x388, 0x03ff03ff }, { 0x390, 0x01230321 },
        { 0x3a4, 0x80 }, { 0x3b8, 0xabcdef },
        { 0x3bc, 0xdeadbeef }, { 0x3c0, 1 },
        { 0x3c4, RAM + 0x1000 }, { 0x3c8, RAM + 0x2000 },
        { 0x3dc, 0x1800 }, { 0x3e8, RAM + 0x3000 },
        { 0x3ec, 0x3000 }, { 0x3f0, 0x6000 },
        { 0x3f4, RAM + 0x9000 }, { 0x3fc, 0x00400040 },
    };
    unsigned i;

    /* Original coefficient loop writes129 zero-extended halfwords. */
    for (i = 0; i < 129; i++) {
        g_assert_cmphex(qtest_readl(q, READOUT_PARAMETERS + i * 4), ==, 0);
        qtest_writel(q, READOUT_PARAMETERS + i * 4, i * 509);
        g_assert_cmphex(qtest_readl(q, READOUT_PARAMETERS + i * 4), ==, i * 509);
    }
    for (i = 0; i < ARRAY_SIZE(fields); i++) {
        uint64_t address = READOUT_PARAMETERS + fields[i].offset;

        g_assert_cmphex(qtest_readl(q, address), ==, 0);
        qtest_writel(q, address, fields[i].value);
        g_assert_cmphex(qtest_readl(q, address), ==, fields[i].value);
    }
    /* Invalid scalar/pair widths must preserve the previous configuration. */
    qtest_writel(q, READOUT_PARAMETERS, 0x10000);
    g_assert_cmphex(qtest_readl(q, READOUT_PARAMETERS), ==, 0);
    qtest_writel(q, READOUT_PARAMETERS + 0x388, 0x040003ff);
    g_assert_cmphex(qtest_readl(q, READOUT_PARAMETERS + 0x388), ==, 0x03ff03ff);
    g_assert_cmpuint(property_at(q, READOUT_PATH, "requests"), ==, 0);
    g_assert_false(qtest_get_irq(q, 0));
    qtest_quit(q);
}

static void test_readout_buffers(void)
{
    QTestState *q = readout_start();
    unsigned selector, bank, word;

    for (selector = 0; selector < 4; selector++) {
        for (bank = 0; bank < 2; bank++) {
            for (word = 0; word < 4; word++) {
                uint64_t address = READOUT_BUFFERS + selector * 0x100 +
                    0x50 + bank * 0x10 + word * 4;
                uint32_t value = RAM + selector * 0x10000 +
                    bank * 0x1000 + word * 0x100;

                g_assert_cmphex(qtest_readl(q, address), ==, 0);
                qtest_writel(q, address, value);
            }
        }
    }
    qtest_writel(q, READOUT_BUFFERS + 0x3c, 1);
    qtest_writel(q, READOUT_BUFFERS + 0x23c, 0x155);
    for (selector = 0; selector < 4; selector++) {
        for (bank = 0; bank < 2; bank++) {
            for (word = 0; word < 4; word++) {
                uint64_t address = READOUT_BUFFERS + selector * 0x100 +
                    0x50 + bank * 0x10 + word * 4;
                uint32_t value = RAM + selector * 0x10000 +
                    bank * 0x1000 + word * 0x100;

                g_assert_cmphex(qtest_readl(q, address), ==, value);
            }
        }
    }
    g_assert_cmpuint(property_at(q, READOUT_PATH, "requests"), ==, 0);
    qtest_qmp_assert_success(q, "{'execute':'system_reset'}");
    qtest_qmp_eventwait(q, "RESET");
    g_assert_cmphex(qtest_readl(q, READOUT_BUFFERS + 0x3c), ==, 0);
    g_assert_cmphex(qtest_readl(q, READOUT_BUFFERS + 0x23c), ==, 0);
    g_assert_cmphex(qtest_readl(q, READOUT_BUFFERS + 0x36c), ==, 0);
    qtest_quit(q);
}

static void test_readout_request(void)
{
    QTestState *q = readout_start();

    qtest_writel(q, READOUT_PARAMETERS + 0x3c0, 1);
    qtest_writel(q, READOUT_PARAMETERS + 0x3c4, RAM + 0x1000);
    qtest_memset(q, RAM + 0x1000, 0xa7, 32);
    g_assert_cmpuint(property_at(q, READOUT_PATH, "requests"), ==, 0);
    qtest_writel(q, READOUT_PARAMETERS + 0x400, 0);
    g_assert_cmpuint(property_at(q, READOUT_PATH, "requests"), ==, 0);
    qtest_writel(q, READOUT_PARAMETERS + 0x400, 1);
    qtest_writel(q, READOUT_PARAMETERS + 0x400, 1);
    g_assert_cmpuint(property_at(q, READOUT_PATH, "requests"), ==, 2);
    g_assert_false(qtest_get_irq(q, 0)); /* Synchronous request pulse finished. */
    g_assert_cmphex(qtest_readl(q, RAM + 0x1000), ==, 0xa7a7a7a7);
    qtest_writel(q, READOUT_PARAMETERS + 0x400, 2);
    g_assert_cmpuint(property_at(q, READOUT_PATH, "requests"), ==, 2);
    g_assert_cmphex(qtest_readl(q, READOUT_PARAMETERS + 0x400), ==, 1);
    qtest_qmp_assert_success(q, "{'execute':'system_reset'}");
    qtest_qmp_eventwait(q, "RESET");
    g_assert_cmpuint(property_at(q, READOUT_PATH, "requests"), ==, 0);
    g_assert_cmphex(qtest_readl(q, READOUT_PARAMETERS + 0x3c0), ==, 0);
    g_assert_cmphex(qtest_readl(q, READOUT_PARAMETERS + 0x400), ==, 0);
    g_assert_cmphex(qtest_readl(q, RAM + 0x1000), ==, 0xa7a7a7a7);
    qtest_quit(q);
}

static void test_readout_provider(void)
{
    QTestState *q = readout_start();

    /* A request is only an observation boundary.  No provider means no
     * ready or completion state, and no DMA/IRQ side effect is invented. */
    qtest_writel(q, READOUT_PARAMETERS + 0x3c0, 0);
    qtest_writel(q, READOUT_PARAMETERS + 0x400, 1);
    g_assert_cmpuint(property_at(q, READOUT_PATH, "request-generation"), ==, 1);
    g_assert_cmpuint(property_at(q, READOUT_PATH, "ready-generation"), ==, 0);
    g_assert_cmpuint(property_at(q, READOUT_PATH, "completion-generation"), ==,
                     0);
    g_assert_cmphex(qtest_readl(q, READOUT_PARAMETERS + 0x3c0), ==, 0);

    /* An explicit provider can publish the source-backed ready observation;
     * this updates only the proven control word and emits an observation
     * pulse.  Completion remains a separate, explicit provider action. */
    qtest_set_irq_in(q, READOUT_PATH, "provider-ready", 0, 1);
    qtest_set_irq_in(q, READOUT_PATH, "provider-ready", 0, 0);
    g_assert_cmpuint(property_at(q, READOUT_PATH, "ready-generation"), ==, 1);
    g_assert_cmphex(qtest_readl(q, READOUT_PARAMETERS + 0x3c0), ==, 1);
    g_assert_cmpuint(property_at(q, READOUT_PATH, "completion-generation"), ==,
                     0);

    qtest_set_irq_in(q, READOUT_PATH, "provider-completion", 0, 1);
    qtest_set_irq_in(q, READOUT_PATH, "provider-completion", 0, 0);
    g_assert_cmpuint(property_at(q, READOUT_PATH, "completion-generation"), ==,
                     1);
    g_assert_cmpuint(property_at(q, READOUT_PATH, "provider-errors"), ==, 0);

    /* A provider pulse before a request is rejected and does not create a
     * generation or a completion. */
    qtest_qmp_assert_success(q, "{'execute':'system_reset'}");
    qtest_qmp_eventwait(q, "RESET");
    qtest_set_irq_in(q, READOUT_PATH, "provider-completion", 0, 1);
    qtest_set_irq_in(q, READOUT_PATH, "provider-completion", 0, 0);
    g_assert_cmpuint(property_at(q, READOUT_PATH, "provider-errors"), ==, 1);
    g_assert_cmpuint(property_at(q, READOUT_PATH, "request-generation"), ==, 0);
    g_assert_cmpuint(property_at(q, READOUT_PATH, "completion-generation"), ==,
                     0);
    qtest_quit(q);
}

#define CHANNEL_PATH "/machine/sdes-channels"
#define CHANNEL_BASE 0x20063000

/* Original VA1078a92c: configuration/control/command/auxiliary per selector. */
static const struct {
    unsigned configuration, control, command, auxiliary;
} channels[] = {
    { 0x000, 0x01c, 0x020, 0x024 },
    { 0x040, 0x05c, 0x060, 0x064 },
    { 0x080, 0x0ac, 0x0b4, 0x0b8 },
    { 0x0d0, 0x0ec, 0x0f0, 0x0f4 },
    { 0x110, 0x12c, 0x130, 0x134 },
    { 0x150, 0x16c, 0x170, 0x174 },
    { 0x190, 0x1ac, 0x1b0, 0x1b4 },
    { 0x1d0, 0x1ec, 0x1f0, 0x1f4 },
    { 0x210, 0x22c, 0x230, 0x234 },
    { 0x250, 0x26c, 0x270, 0x274 },
    { 0x290, 0x2ac, 0x2b0, 0x2b4 },
};

static QTestState *channels_start(const char *outputs)
{
    g_autofree char *bios = g_shell_quote(image);
    QTestState *q = qtest_initf(
        "-M z7ii-mas-diagnostic -smp 1 -bios %s -serial none", bios);

    qtest_irq_intercept_out_named(q, CHANNEL_PATH, outputs);
    return q;
}

static void test_channels_configuration(void)
{
    QTestState *q = channels_start("command-publish");
    unsigned i, word;

    for (i = 0; i < ARRAY_SIZE(channels); i++) {
        unsigned words = i == 2 ? 11 : 7;

        for (word = 0; word < words; word++) {
            uint64_t address = CHANNEL_BASE + channels[i].configuration +
                               word * 4;
            uint32_t value = word == (i == 2 ? 6 : 2) ? 0x789a :
                             0x90000000 | (i << 16) | word;

            g_assert_cmphex(qtest_readl(q, address), ==, 0);
            qtest_writel(q, address, value);
        }
        qtest_writel(q, CHANNEL_BASE + channels[i].control,
                     i == 2 ? 0x93 : 0xfedcba98);
        if (i == 2) {
            qtest_writel(q, CHANNEL_BASE + channels[i].control + 4,
                         0x76543210);
        }
        qtest_writel(q, CHANNEL_BASE + channels[i].auxiliary, 0x80000000 | i);
    }
    /* Check after all writes: the selector4 extended layout must not alias
     * the following channel or either of its two independent controls.
     */
    for (i = 0; i < ARRAY_SIZE(channels); i++) {
        unsigned words = i == 2 ? 11 : 7;

        for (word = 0; word < words; word++) {
            uint64_t address = CHANNEL_BASE + channels[i].configuration +
                               word * 4;
            uint32_t value = word == (i == 2 ? 6 : 2) ? 0x789a :
                             0x90000000 | (i << 16) | word;

            g_assert_cmphex(qtest_readl(q, address), ==, value);
        }
        g_assert_cmphex(qtest_readl(q, CHANNEL_BASE + channels[i].control), ==,
                        i == 2 ? 0x93 : 0xfedcba98);
        g_assert_cmphex(qtest_readl(q, CHANNEL_BASE + channels[i].auxiliary),
                        ==, 0x80000000 | i);
        g_assert_false(qtest_get_irq(q, i));
    }
    g_assert_cmphex(qtest_readl(q, CHANNEL_BASE + 0xb0), ==, 0x76543210);
    /* Actual original first fault is the final channel's packed first word. */
    qtest_writel(q, CHANNEL_BASE + 0x290, 0x0323029b);
    g_assert_cmphex(qtest_readl(q, CHANNEL_BASE + 0x290), ==, 0x0323029b);
    qtest_writel(q, CHANNEL_BASE + 8, 0x10000);
    qtest_writel(q, CHANNEL_BASE + 0x98, 0x10000);
    qtest_writel(q, CHANNEL_BASE + 0xac, 0x100);
    qtest_writew(q, CHANNEL_BASE + 0x290, 0xffff);
    qtest_writeb(q, CHANNEL_BASE + 0x290, 0xff);
    qtest_writel(q, CHANNEL_BASE + 0x28, 1); /* Unknown aperture gap. */
    g_assert_cmphex(qtest_readl(q, CHANNEL_BASE + 8), ==, 0x789a);
    g_assert_cmphex(qtest_readl(q, CHANNEL_BASE + 0x98), ==, 0x789a);
    g_assert_cmphex(qtest_readl(q, CHANNEL_BASE + 0xac), ==, 0x93);
    g_assert_cmphex(qtest_readl(q, CHANNEL_BASE + 0x290), ==, 0x0323029b);
    g_assert_cmpuint(property_at(q, CHANNEL_PATH, "requests"), ==, 0);
    qtest_quit(q);
}

static void test_channels_request(void)
{
    QTestState *q = channels_start("command-publish");
    unsigned i;

    qtest_memset(q, RAM + 0x1000, 0xa7, 32);
    for (i = 0; i < ARRAY_SIZE(channels); i++) {
        uint64_t command = CHANNEL_BASE + channels[i].command;

        qtest_writel(q, CHANNEL_BASE + channels[i].configuration, RAM + 0x1000);
        qtest_writel(q, CHANNEL_BASE + channels[i].control, 1);
        qtest_writel(q, CHANNEL_BASE + channels[i].auxiliary, UINT32_MAX);
        g_assert_cmpuint(property_at(q, CHANNEL_PATH, "requests"), ==, i * 2);
        qtest_writel(q, command, 1);
        qtest_writel(q, command, 1);
        g_assert_cmpuint(property_at(q, CHANNEL_PATH, "requests"), ==,
                         (i + 1) * 2);
        g_assert_false(qtest_get_irq(q, i)); /* Synchronous pulse has ended. */
        qtest_writel(q, command, 0);
        qtest_writel(q, command, 2);
        g_assert_cmpuint(property_at(q, CHANNEL_PATH, "requests"), ==,
                         (i + 1) * 2);
        g_assert_cmphex(qtest_readl(q, command), ==, 1);
    }
    g_assert_cmphex(qtest_readl(q, RAM + 0x1000), ==, 0xa7a7a7a7);
    qtest_quit(q);
}

static void test_channels_reset(void)
{
    QTestState *q = channels_start("command-publish");
    unsigned i, word;

    for (i = 0; i < ARRAY_SIZE(channels); i++) {
        unsigned words = i == 2 ? 11 : 7;

        for (word = 0; word < words; word++) {
            qtest_writel(q, CHANNEL_BASE + channels[i].configuration + word * 4,
                         0x1234);
        }
        qtest_writel(q, CHANNEL_BASE + channels[i].control, 0x56);
        if (i == 2) {
            qtest_writel(q, CHANNEL_BASE + channels[i].control + 4, 0x12345678);
        }
        qtest_writel(q, CHANNEL_BASE + channels[i].auxiliary, UINT32_MAX);
        qtest_writel(q, CHANNEL_BASE + channels[i].command, 1);
    }
    g_assert_cmpuint(property_at(q, CHANNEL_PATH, "requests"), ==,
                     ARRAY_SIZE(channels));
    qtest_qmp_assert_success(q, "{'execute':'system_reset'}");
    qtest_qmp_eventwait(q, "RESET");
    for (i = 0; i < ARRAY_SIZE(channels); i++) {
        unsigned words = i == 2 ? 11 : 7;

        for (word = 0; word < words; word++) {
            g_assert_cmphex(qtest_readl(q, CHANNEL_BASE +
                            channels[i].configuration + word * 4), ==, 0);
        }
        g_assert_cmphex(qtest_readl(q, CHANNEL_BASE + channels[i].control), ==, 0);
        g_assert_cmphex(qtest_readl(q, CHANNEL_BASE + channels[i].command), ==, 0);
        g_assert_cmphex(qtest_readl(q, CHANNEL_BASE + channels[i].auxiliary),
                        ==, 0);
        g_assert_false(qtest_get_irq(q, i));
    }
    g_assert_cmphex(qtest_readl(q, CHANNEL_BASE + 0xb0), ==, 0);
    g_assert_cmpuint(property_at(q, CHANNEL_PATH, "requests"), ==, 0);
    qtest_quit(q);
}

static void test_channels_global(void)
{
    QTestState *q = channels_start("global-command-publish");
    static const unsigned fields[] = { 0x2d0, 0x2e0, 0x2ec, 0x2f4, 0x2f8 };
    unsigned i;

    qtest_memset(q, RAM + 0x1000, 0xa7, 32);
    for (i = 0; i < ARRAY_SIZE(fields); i++) {
        uint64_t address = CHANNEL_BASE + fields[i];
        uint32_t value = fields[i] == 0x2ec ? 0xfff : 0xfedcba98;

        g_assert_cmphex(qtest_readl(q, address), ==, 0);
        qtest_writel(q, address, value);
        g_assert_cmphex(qtest_readl(q, address), ==, value);
    }
    /* Replay both actual OR-mask protocols without manufacturing a response. */
    for (i = 0; i < 2; i++) {
        uint64_t configuration = CHANNEL_BASE + 0x2d0 + i * 0x10;
        uint64_t command = configuration + 4;

        qtest_writel(q, configuration, 0);
        qtest_writel(q, configuration, qtest_readl(q, configuration) | 0x400);
        qtest_writel(q, configuration, qtest_readl(q, configuration) | 3);
        g_assert_cmphex(qtest_readl(q, configuration), ==, 0x403);
        g_assert_cmpuint(property_at(q, CHANNEL_PATH, "global-requests"), ==,
                         i * 2);
        qtest_writel(q, command, 1);
        qtest_writel(q, command, 1);
        qtest_writel(q, command, 0);
        qtest_writel(q, command, 2);
        g_assert_cmpuint(property_at(q, CHANNEL_PATH, "global-requests"), ==,
                         (i + 1) * 2);
        g_assert_cmphex(qtest_readl(q, command), ==, 1);
        g_assert_false(qtest_get_irq(q, i));
    }
    qtest_writel(q, CHANNEL_BASE + 0x2ec, 0x1000);
    qtest_writew(q, CHANNEL_BASE + 0x2d0, 0xffff);
    qtest_writel(q, CHANNEL_BASE + 0x2d8, UINT32_MAX); /* Unqualified offset. */
    g_assert_cmphex(qtest_readl(q, CHANNEL_BASE + 0x2ec), ==, 0xfff);
    g_assert_cmphex(qtest_readl(q, CHANNEL_BASE + 0x2d0), ==, 0x403);
    g_assert_cmpuint(property_at(q, CHANNEL_PATH, "requests"), ==, 0);
    g_assert_cmphex(qtest_readl(q, RAM + 0x1000), ==, 0xa7a7a7a7);
    qtest_qmp_assert_success(q, "{'execute':'system_reset'}");
    qtest_qmp_eventwait(q, "RESET");
    for (i = 0; i < ARRAY_SIZE(fields); i++) {
        g_assert_cmphex(qtest_readl(q, CHANNEL_BASE + fields[i]), ==, 0);
    }
    for (i = 0; i < 2; i++) {
        g_assert_cmphex(qtest_readl(q, CHANNEL_BASE + 0x2d4 + i * 0x10), ==, 0);
        g_assert_false(qtest_get_irq(q, i));
    }
    g_assert_cmpuint(property_at(q, CHANNEL_PATH, "global-requests"), ==, 0);
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
    qtest_add_func("/nikon/sensor/capture/configuration",
                    test_capture_configuration);
    qtest_add_func("/nikon/sensor/capture/packet", test_capture_packet);
    qtest_add_func("/nikon/sensor/capture/banks", test_capture_banks);
    qtest_add_func("/nikon/sensor/capture/group", test_capture_group);
    qtest_add_func("/nikon/sensor/capture/group-results",
                   test_capture_group_results);
    qtest_add_func("/nikon/sensor/capture/group-cursor",
                   test_capture_group_cursor);
    qtest_add_func("/nikon/sensor/capture/parameters", test_capture_parameters);
    qtest_add_func("/nikon/sensor/capture/endpoints", test_capture_endpoints);
    qtest_add_func("/nikon/sensor/capture/endpoint-globals",
                    test_capture_endpoint_globals);
    qtest_add_func("/nikon/sensor/capture/controls", test_capture_controls);
    qtest_add_func("/nikon/sensor/capture/runstop", test_capture_runstop);
    qtest_add_func("/nikon/sensor/capture/status", test_capture_status);
    qtest_add_func("/nikon/sensor/capture/publication", test_capture_publication);
    qtest_add_func("/nikon/sensor/readout/configuration",
                    test_readout_configuration);
    qtest_add_func("/nikon/sensor/readout/buffers", test_readout_buffers);
    qtest_add_func("/nikon/sensor/readout/request", test_readout_request);
    qtest_add_func("/nikon/sensor/readout/provider", test_readout_provider);
    qtest_add_func("/nikon/sensor/channels/configuration",
                    test_channels_configuration);
    qtest_add_func("/nikon/sensor/channels/request", test_channels_request);
    qtest_add_func("/nikon/sensor/channels/reset", test_channels_reset);
    qtest_add_func("/nikon/sensor/channels/global", test_channels_global);
    qtest_add_func("/nikon/sensor/eye", test_eye_sensor);
    qtest_add_func("/nikon/sensor/eye/nak", test_eye_sensor_nak);
    qtest_add_func("/nikon/sensor/display-buffer", test_sensor_display_buffer);
    result = g_test_run();
    unlink(image);
    unlink(frame);
    g_free(image);
    g_free(frame);
    return result;
}

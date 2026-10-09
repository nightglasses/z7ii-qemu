/* Real socket backpressure and source-driven wire failure/reconnect.
 * Synthetic endpoint replies only; no original application ACK claim.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "qemu/cutils.h"
#include "qemu/sockets.h"
#include "qobject/qdict.h"
#include "hw/nikon/wire.h"
#include "libqtest.h"

#define GPIO 0x04282000
#define I2C 0x2c018000
#define STEP_NS 100000
#define MAX_PAIRS 16384

void nikon_pressure_add_tests(const char *image);

static const char *bios;

typedef struct PressureTest {
    QTestState *q;
    char *dir;
    char *path;
    int listener, fd;
} PressureTest;

static PressureTest pressure_start(void)
{
    PressureTest t = { .listener = -1, .fd = -1 };
    struct sockaddr_un address = { .sun_family = AF_UNIX };
    GPollFD pfd;

    t.dir = g_dir_make_tmp("qtest-pressure-XXXXXX", NULL);
    g_assert_nonnull(t.dir);
    t.path = g_strdup_printf("%s/wire", t.dir);
    g_assert_cmpuint(strlen(t.path), <, sizeof(address.sun_path));
    pstrcpy(address.sun_path, sizeof(address.sun_path), t.path);
    t.listener = qemu_socket(AF_UNIX, SOCK_STREAM, 0);
    g_assert_cmpint(t.listener, >=, 0);
    g_assert_cmpint(bind(t.listener, (struct sockaddr *)&address,
                         sizeof(address)), ==, 0);
    g_assert_cmpint(listen(t.listener, 1), ==, 0);
    t.q = qtest_initf("-machine z7ii-mas-diagnostic -smp 1 -bios %s "
        "-serial none -display none -chardev socket,id=p0,path=%s,reconnect-ms=250 "
        "-device nikon-peer,id=wire0,chardev=p0 "
        "-device nikon-i2c-peer,peer=/machine/peripheral/wire0,i2c=/machine/i2c0,"
        "gpio=/machine/gpio,control=173,notify=102", bios, t.path);
    pfd = (GPollFD) { .fd = t.listener, .events = G_IO_IN };
    g_assert_cmpint(g_poll(&pfd, 1, 1000), ==, 1);
    t.fd = accept(t.listener, NULL, NULL);
    g_assert_cmpint(t.fd, >=, 0);
    qtest_qmp_assert_success(t.q, "{'execute':'cont'}");
    return t;
}

static void pressure_quit(PressureTest *t)
{
    qtest_quit(t->q);
    close(t->fd);
    close(t->listener);
    unlink(t->path);
    rmdir(t->dir);
    g_free(t->path);
    g_free(t->dir);
}

static bool connected(PressureTest *t)
{
    /* This is the official SocketChardev property, not a peer test seam. */
    QDict *response = qtest_qmp(t->q,
        "{'execute':'qom-get','arguments':{'path':'/chardevs/p0',"
        "'property':'connected'}}");
    bool value;

    g_assert_false(qdict_haskey(response, "error"));
    value = qdict_get_bool(response, "return");
    qobject_unref(response);
    return value;
}

static void wait_connected(PressureTest *t)
{
    int64_t deadline = g_get_monotonic_time() + G_TIME_SPAN_SECOND;

    /* accept() and the QIO main-loop OPEN callback are distinct events. */
    while (!connected(t)) {
        g_assert_cmpint(g_get_monotonic_time(), <, deadline);
        g_usleep(1000);
    }
}

static void read_frame(PressureTest *t, uint8_t *frame)
{
    GPollFD pfd = { .fd = t->fd, .events = G_IO_IN };
    int64_t deadline = g_get_monotonic_time() + G_TIME_SPAN_SECOND;
    unsigned used = 0;

    while (used < NIKON_WIRE_SIZE) {
        ssize_t count;

        while (!g_poll(&pfd, 1, 0)) {
            g_assert_cmpint(g_get_monotonic_time(), <, deadline);
            qtest_clock_step(t->q, STEP_NS);
            g_usleep(1000);
        }
        count = recv(t->fd, frame + used, NIKON_WIRE_SIZE - used, 0);
        g_assert_cmpint(count, >, 0);
        used += count;
    }
    g_assert_cmphex(ldl_le_p(frame), ==, NIKON_WIRE_MAGIC);
    g_assert_cmpuint(frame[NIKON_WIRE_RESERVED], ==, 0);
    g_assert_cmphex(ldq_le_p(frame + NIKON_WIRE_PADDING), ==, 0);
}

static void control_frame(const uint8_t *frame, bool high)
{
    g_assert_cmphex(ldl_le_p(frame), ==, NIKON_WIRE_MAGIC);
    g_assert_cmpuint(frame[NIKON_WIRE_TYPE], ==, NIKON_WIRE_SIGNAL);
    g_assert_cmpuint(frame[NIKON_WIRE_KIND], ==, NIKON_PEER_GPIO);
    g_assert_cmpuint(frame[NIKON_WIRE_BITS], ==, 0);
    g_assert_cmpuint(frame[NIKON_WIRE_RESERVED], ==, 0);
    g_assert_cmphex(ldq_le_p(frame + NIKON_WIRE_TOKEN), ==, 0);
    g_assert_cmphex(ldl_le_p(frame + NIKON_WIRE_LINE), ==, NIKON_TOUCH_CONTROL);
    g_assert_cmphex(ldl_le_p(frame + NIKON_WIRE_WORD), ==, high);
    g_assert_cmphex(ldq_le_p(frame + NIKON_WIRE_PADDING), ==, 0);
}

static void request(PressureTest *t, NikonI2CPhase phase, uint8_t value,
                     uint8_t *frame)
{
    read_frame(t, frame);
    g_assert_cmpuint(frame[NIKON_WIRE_TYPE], ==, NIKON_WIRE_REQUEST);
    g_assert_cmpuint(frame[NIKON_WIRE_KIND], ==, NIKON_PEER_I2C);
    g_assert_cmpuint(frame[NIKON_WIRE_BITS], ==, 8);
    g_assert_cmphex(ldq_le_p(frame + NIKON_WIRE_TOKEN), >, 0);
    g_assert_cmphex(ldl_le_p(frame + NIKON_WIRE_LINE), ==, phase);
    g_assert_cmphex(ldl_le_p(frame + NIKON_WIRE_WORD), ==, value);
}

static void reply(PressureTest *t, const uint8_t *request)
{
    uint8_t frame[NIKON_WIRE_SIZE];

    memcpy(frame, request, sizeof(frame));
    frame[NIKON_WIRE_TYPE] = NIKON_WIRE_REPLY;
    stl_le_p(frame + NIKON_WIRE_WORD, 0);
    g_assert_cmpint(send(t->fd, frame, sizeof(frame), 0), ==, sizeof(frame));
}

static void quiet(PressureTest *t)
{
    GPollFD pfd = { .fd = t->fd, .events = G_IO_IN };

    qtest_clock_step(t->q, STEP_NS * 100);
    g_assert_true(connected(t));
    g_assert_cmpint(g_poll(&pfd, 1, 0), ==, 0);
}

static unsigned drain_closed(PressureTest *t)
{
    uint8_t frame[NIKON_WIRE_SIZE];
    unsigned used = 0, frames = 0;
    int64_t deadline = g_get_monotonic_time() + G_TIME_SPAN_SECOND;
    GPollFD pfd = { .fd = t->fd, .events = G_IO_IN };

    for (;;) {
        ssize_t count;

        g_assert_cmpint(g_get_monotonic_time(), <, deadline);
        g_assert_cmpint(g_poll(&pfd, 1, 100), ==, 1);
        count = recv(t->fd, frame + used, sizeof(frame) - used, 0);
        g_assert_cmpint(count, >=, 0);
        if (!count) {
            /* A close can truncate the final frame; it must not be completed
             * from a discarded queue tail or replayed on a new connection. */
            return frames;
        }
        used += count;
        if (used == sizeof(frame)) {
            control_frame(frame, frames & 1); /* Genuine LOW/HIGH prefix. */
            frames++;
            used = 0;
        }
    }
}

static void test_pressure(void)
{
    PressureTest t = pressure_start();
    uint8_t old[NIKON_WIRE_SIZE], frame[NIKON_WIRE_SIZE];
    unsigned pairs = 0, received;
    int64_t deadline;
    GPollFD pfd;

    wait_connected(&t);
    /* AA is actual GPIO10[13]: only a driven GPIO output emits CONTROL. */
    qtest_writel(t.q, GPIO + 0x28, 0x20002000);
    qtest_writel(t.q, GPIO + 0x128, 0x2000);
    read_frame(&t, frame);
    control_frame(frame, true);
    qtest_writel(t.q, I2C, 6);
    qtest_writel(t.q, I2C + 0xc, 0x38);
    qtest_writel(t.q, I2C + 8, 0x55);
    request(&t, NIKON_I2C_START, 0x55, old);

    /* The real peer stops reading. No test queue sizes or ready knobs exist. */
    deadline = g_get_monotonic_time() + 5 * G_TIME_SPAN_SECOND;
    while (connected(&t)) {
        g_assert_cmpuint(pairs, <, MAX_PAIRS);
        g_assert_cmpint(g_get_monotonic_time(), <, deadline);
        for (unsigned i = 0; i < 8; i++) {
            qtest_writel(t.q, GPIO + 0x28, 0x20000000);
            qtest_writel(t.q, GPIO + 0x28, 0x20002000);
            pairs++;
        }
    }
    g_assert_cmphex(qtest_readl(t.q, I2C + 4) & 0x140, ==, 0x140);
    g_assert_cmphex(qtest_readl(t.q, I2C + 0x10), ==, 0);
    /* Disable actual drive before native reconnect; stale HIGH is not replay. */
    qtest_writel(t.q, GPIO + 0x128, 0);
    qtest_writel(t.q, GPIO + 0x228, 0x2000);
    received = drain_closed(&t);
    g_assert_cmpuint(received, >, 0);
    g_assert_cmpuint(received, <, 2 * pairs);
    close(t.fd);
    pfd = (GPollFD) { .fd = t.listener, .events = G_IO_IN };
    g_assert_cmpint(g_poll(&pfd, 1, 1000), ==, 1);
    t.fd = accept(t.listener, NULL, NULL);
    g_assert_cmpint(t.fd, >=, 0);
    wait_connected(&t);
    quiet(&t); /* Neither stale CONTROL nor an accepted START retry. */
    reply(&t, old);
    quiet(&t);
    g_assert_cmphex(qtest_readl(t.q, I2C + 4) & 0x140, ==, 0x140);
    g_assert_cmphex(qtest_readl(t.q, I2C + 0x10), ==, 0);

    /* Only real guest cancellation can replace the unfinished transaction. */
    qtest_writel(t.q, I2C, 0x46);
    g_assert_cmphex(qtest_readl(t.q, I2C + 4) & 0x160, ==, 0);
    qtest_writel(t.q, I2C + 0xc, 0x39);
    qtest_writel(t.q, I2C + 8, 0x55);
    request(&t, NIKON_I2C_START, 0x55, frame);
    g_assert_cmphex(ldq_le_p(frame + NIKON_WIRE_TOKEN), !=,
                    ldq_le_p(old + NIKON_WIRE_TOKEN));
    reply(&t, frame);
    request(&t, NIKON_I2C_WRITE, 0x39, frame);
    reply(&t, frame);
    request(&t, NIKON_I2C_STOP, 0, frame);
    reply(&t, frame);
    deadline = g_get_monotonic_time() + G_TIME_SPAN_SECOND;
    while (qtest_readl(t.q, I2C + 4) & 0x100) {
        g_assert_cmpint(g_get_monotonic_time(), <, deadline);
        qtest_clock_step(t.q, STEP_NS);
        g_usleep(1000);
    }
    g_assert_cmphex(qtest_readl(t.q, I2C + 0x10) & 5, ==, 1);
    quiet(&t);
    pressure_quit(&t);
}

void nikon_pressure_add_tests(const char *image)
{
    bios = image;
    qtest_add_func("/nikon/peer/pressure", test_pressure);
}

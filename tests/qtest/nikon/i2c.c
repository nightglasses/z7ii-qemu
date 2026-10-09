/* Whole I2C bus lifecycle through native SSI-independent peer sockets.
 * Synthetic endpoint bytes only; no TPJ firmware/application ACK claim.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "qemu/cutils.h"
#include "qemu/sockets.h"
#include "hw/nikon/wire.h"
#include "libqtest.h"
#include "fixture.h"

#define BASE 0x2c018000
#define STEP_NS 100000
#define IRQ_MASK 0x2ff
#define GPIO 0x04282000
#define EXIU 0x0426f000

void nikon_i2c_add_tests(const char *image);

static const char *bios;

typedef struct I2CWireTest {
    QTestState *q;
    char *dir;
    int fd[2];
    int listener[2];
    unsigned count;
} I2CWireTest;

static I2CWireTest wire_start_gpio(unsigned count, bool gpio)
{
    I2CWireTest t = {
        .fd = { -1, -1 }, .listener = { -1, -1 }, .count = count,
    };
    g_autoptr(GString) args = g_string_new(NULL);

    g_assert_cmpuint(count, <=, ARRAY_SIZE(t.fd));
    g_assert_true(!gpio || count == 1);
    t.dir = g_dir_make_tmp("qtest-i2c-XXXXXX", NULL);
    g_assert_nonnull(t.dir);
    g_string_append_printf(args, "-machine z7ii-mas-diagnostic -smp 1 "
                           "-bios %s -serial none -display none", bios);
    for (unsigned i = 0; i < count; i++) {
        struct sockaddr_un address = { .sun_family = AF_UNIX };
        g_autofree char *path = g_strdup_printf("%s/%u", t.dir, i);

        g_assert_cmpuint(strlen(path), <, sizeof(address.sun_path));
        pstrcpy(address.sun_path, sizeof(address.sun_path), path);
        t.listener[i] = qemu_socket(AF_UNIX, SOCK_STREAM, 0);
        g_assert_cmpint(t.listener[i], >=, 0);
        g_assert_cmpint(bind(t.listener[i], (struct sockaddr *)&address,
                             sizeof(address)), ==, 0);
        g_assert_cmpint(listen(t.listener[i], 1), ==, 0);
        g_string_append_printf(args,
            " -chardev socket,id=p%u,path=%s,reconnect-ms=10"
            " -device nikon-peer,id=wire%u,chardev=p%u"
            " -device nikon-i2c-peer,id=link%u,peer=/machine/peripheral/wire%u,"
            "i2c=/machine/i2c%u", i, path, i, i, i, i, i);
        if (gpio) {
            g_string_append(args, ",gpio=/machine/gpio,control=173,notify=102");
        }
    }
    t.q = qtest_init(args->str);
    for (unsigned i = 0; i < count; i++) {
        GPollFD pfd = { .fd = t.listener[i], .events = G_IO_IN };

        g_assert_cmpint(g_poll(&pfd, 1, 1000), ==, 1);
        t.fd[i] = accept(t.listener[i], NULL, NULL);
        g_assert_cmpint(t.fd[i], >=, 0);
    }
    qtest_qmp_assert_success(t.q, "{'execute':'cont'}");
    return t;
}

static I2CWireTest wire_start(unsigned count)
{
    return wire_start_gpio(count, false);
}

static void wire_quit(I2CWireTest *t)
{
    qtest_quit(t->q);
    for (unsigned i = 0; i < t->count; i++) {
        g_autofree char *path = g_strdup_printf("%s/%u", t->dir, i);

        close(t->fd[i]);
        close(t->listener[i]);
        unlink(path);
    }
    rmdir(t->dir);
    g_free(t->dir);
}

static uint32_t read_reg(QTestState *q, unsigned port, unsigned offset)
{
    return qtest_readl(q, BASE + port * 0x1000 + offset);
}

static void write_reg(QTestState *q, unsigned port, unsigned offset, uint32_t value)
{
    qtest_writel(q, BASE + port * 0x1000 + offset, value);
}

static void wait_value(QTestState *q, uint64_t address,
                       uint32_t mask, uint32_t expected)
{
    int64_t deadline = g_get_monotonic_time() + G_TIME_SPAN_SECOND;

    while ((qtest_readl(q, address) & mask) != expected) {
        g_assert_cmpint(g_get_monotonic_time(), <, deadline);
        qtest_clock_step(q, STEP_NS);
        g_usleep(1000);
    }
}

static void wait_reg(QTestState *q, unsigned port, unsigned offset,
                      uint32_t mask, uint32_t expected)
{
    wait_value(q, BASE + port * 0x1000 + offset, mask, expected);
}

static void wire_frame(I2CWireTest *t, unsigned port, uint8_t *frame)
{
    GPollFD pfd = { .fd = t->fd[port], .events = G_IO_IN };
    int64_t deadline = g_get_monotonic_time() + G_TIME_SPAN_SECOND;
    unsigned received = 0;

    while (received < NIKON_WIRE_SIZE) {
        ssize_t count;

        while (!g_poll(&pfd, 1, 0)) {
            g_assert_cmpint(g_get_monotonic_time(), <, deadline);
            qtest_clock_step(t->q, STEP_NS);
            g_usleep(1000);
        }
        count = recv(pfd.fd, frame + received, NIKON_WIRE_SIZE - received, 0);
        g_assert_cmpint(count, >, 0);
        received += count;
    }
    g_assert_cmphex(ldl_le_p(frame), ==, NIKON_WIRE_MAGIC);
    g_assert_cmpuint(frame[NIKON_WIRE_RESERVED], ==, 0);
    g_assert_cmphex(ldq_le_p(frame + NIKON_WIRE_PADDING), ==, 0);
}

static void wire_read(I2CWireTest *t, unsigned port, uint8_t *frame)
{
    wire_frame(t, port, frame);
    g_assert_cmpuint(frame[NIKON_WIRE_KIND], ==, NIKON_PEER_I2C);
    g_assert_cmpuint(frame[NIKON_WIRE_BITS], ==, 8);
    g_assert_cmpuint(ldq_le_p(frame + NIKON_WIRE_TOKEN), >, 0);
}

static void wire_request(I2CWireTest *t, unsigned port, NikonI2CPhase phase,
                          uint8_t value, uint8_t *frame)
{
    wire_read(t, port, frame);
    g_assert_cmpuint(frame[NIKON_WIRE_TYPE], ==, NIKON_WIRE_REQUEST);
    g_assert_cmphex(ldl_le_p(frame + NIKON_WIRE_LINE), ==, phase);
    g_assert_cmphex(ldl_le_p(frame + NIKON_WIRE_WORD), ==, value);
}

static void wire_reply(I2CWireTest *t, unsigned port, const uint8_t *request,
                       uint32_t result)
{
    uint8_t reply[NIKON_WIRE_SIZE];

    memcpy(reply, request, sizeof(reply));
    reply[NIKON_WIRE_TYPE] = NIKON_WIRE_REPLY;
    stl_le_p(reply + NIKON_WIRE_WORD, result);
    /* Split within the token, exercising the actual streaming frame parser. */
    g_assert_cmpint(send(t->fd[port], reply, 11, 0), ==, 11);
    g_assert_cmpint(send(t->fd[port], reply + 11,
                         NIKON_WIRE_SIZE - 11, 0), ==, NIKON_WIRE_SIZE - 11);
}

static void wire_quiet(I2CWireTest *t, unsigned port)
{
    GPollFD pfd = { .fd = t->fd[port], .events = G_IO_IN };

    qtest_clock_step(t->q, STEP_NS * 100);
    g_assert_cmpint(g_poll(&pfd, 1, 0), ==, 0);
}

static void begin_tx(QTestState *q, unsigned port, uint8_t value, bool hold)
{
    write_reg(q, port, 0, hold ? 0x16 : 6);
    write_reg(q, port, 0xc, value);
    write_reg(q, port, 8, 0x55);
}

static void stop_reply(I2CWireTest *t, unsigned port)
{
    uint8_t frame[NIKON_WIRE_SIZE];

    wire_request(t, port, NIKON_I2C_STOP, 0, frame);
    g_assert_cmphex(read_reg(t->q, port, 4) & 0x100, ==, 0x100);
    wire_reply(t, port, frame, 0);
    wait_reg(t->q, port, 4, 0x100, 0);
}

static void source_transaction(I2CWireTest *t, uint8_t command,
                                const uint8_t *bytes, unsigned length)
{
    uint8_t frame[NIKON_WIRE_SIZE];

    write_reg(t->q, 0, 0x10, IRQ_MASK);
    begin_tx(t->q, 0, command, true);
    wire_request(t, 0, NIKON_I2C_START, 0x55, frame);
    wire_quiet(t, 0);
    g_assert_cmphex(read_reg(t->q, 0, 4) & 0x40, ==, 0x40);
    g_assert_cmphex(read_reg(t->q, 0, 0x10) & 5, ==, 0);
    wire_reply(t, 0, frame, 0);
    wire_request(t, 0, NIKON_I2C_WRITE, command, frame);
    g_assert_cmphex(read_reg(t->q, 0, 4) & 0x40, ==, 0x40);
    wire_reply(t, 0, frame, 0);
    wait_reg(t->q, 0, 4, 0x40, 0);
    g_assert_cmphex(read_reg(t->q, 0, 4) & 0x100, ==, 0x100);
    g_assert_cmphex(read_reg(t->q, 0, 0x10) & 1, ==, 0);
    write_reg(t->q, 0, 0x10, IRQ_MASK);
    write_reg(t->q, 0, 0, 0x17);
    write_reg(t->q, 0, 0x14, length);
    write_reg(t->q, 0, 8, 0x55);
    wire_request(t, 0, NIKON_I2C_START, 0xd5, frame);
    /* Release HOLD while the repeated address is pending, without early STOP. */
    write_reg(t->q, 0, 0, 7);
    wire_reply(t, 0, frame, 0);
    for (unsigned i = 0; i < length; i++) {
        wire_request(t, 0, NIKON_I2C_READ, 0, frame);
        g_assert_cmphex(read_reg(t->q, 0, 0x14), ==, length - i);
        wire_reply(t, 0, frame, bytes[i]);
    }
    wire_request(t, 0, NIKON_I2C_NACK, 0, frame);
    g_assert_cmphex(read_reg(t->q, 0, 0x10) & 1, ==, 0);
    g_assert_false(qtest_get_irq(t->q, 0));
    wire_reply(t, 0, frame, 0);
    wire_request(t, 0, NIKON_I2C_STOP, 0, frame);
    g_assert_cmphex(read_reg(t->q, 0, 0x10) & 1, ==, 0);
    g_assert_false(qtest_get_irq(t->q, 0));
    wire_reply(t, 0, frame, 0);
    wait_reg(t->q, 0, 4, 0x100, 0);
    g_assert_cmphex(read_reg(t->q, 0, 0x10) & 1, ==, 1);
    g_assert_true(qtest_get_irq(t->q, 0));
    for (unsigned i = 0; i < length; i++) {
        g_assert_cmphex(read_reg(t->q, 0, 0xc), ==, bytes[i]);
    }
    write_reg(t->q, 0, 0x10, 1);
    g_assert_false(qtest_get_irq(t->q, 0));
}

static void test_source(void)
{
    I2CWireTest t = wire_start(1);
    const uint8_t status[] = { 0x23, 0xff };
    const uint8_t block[] = { 0x42, 0, 0x17, 0x88, 0xff, 0x61, 0x5a };

    qtest_irq_intercept_out_named(t.q, "/machine/i2c0", "sysbus-irq");
    write_reg(t.q, 0, 0x24, 1);
    source_transaction(&t, 0x10, status, ARRAY_SIZE(status));
    source_transaction(&t, 0x11, block, ARRAY_SIZE(block));
    wire_quit(&t);
}

static void test_nak(void)
{
    I2CWireTest t = wire_start(1);
    uint8_t frame[NIKON_WIRE_SIZE];

    begin_tx(t.q, 0, 0x73, false);
    wire_request(&t, 0, NIKON_I2C_START, 0x55, frame);
    wire_reply(&t, 0, frame, NIKON_I2C_NAK);
    stop_reply(&t, 0);
    g_assert_cmphex(read_reg(t.q, 0, 0x10) & 5, ==, 4);
    g_assert_cmphex(read_reg(t.q, 0, 4) & 0x40, ==, 0x40);
    write_reg(t.q, 0, 0, 0x46);
    write_reg(t.q, 0, 0x10, IRQ_MASK);
    begin_tx(t.q, 0, 0x74, false);
    wire_request(&t, 0, NIKON_I2C_START, 0x55, frame);
    wire_reply(&t, 0, frame, 0);
    wire_request(&t, 0, NIKON_I2C_WRITE, 0x74, frame);
    wire_reply(&t, 0, frame, NIKON_I2C_NAK);
    stop_reply(&t, 0);
    g_assert_cmphex(read_reg(t.q, 0, 0x10) & 5, ==, 4);
    g_assert_cmphex(read_reg(t.q, 0, 4) & 0x40, ==, 0);
    write_reg(t.q, 0, 0x10, IRQ_MASK);
    write_reg(t.q, 0, 0, 7);
    write_reg(t.q, 0, 0x14, 2);
    write_reg(t.q, 0, 8, 0x55);
    wire_request(&t, 0, NIKON_I2C_START, 0xd5, frame);
    wire_reply(&t, 0, frame, 0);
    wire_request(&t, 0, NIKON_I2C_READ, 0, frame);
    wire_reply(&t, 0, frame, NIKON_I2C_NAK);
    stop_reply(&t, 0);
    g_assert_cmphex(read_reg(t.q, 0, 0x10) & 5, ==, 4);
    g_assert_cmphex(read_reg(t.q, 0, 4) & 0x20, ==, 0);
    g_assert_cmphex(read_reg(t.q, 0, 0x14), ==, 2);
    wire_quit(&t);
}

static void test_cancel(void)
{
    I2CWireTest t = wire_start(1);
    uint8_t old[NIKON_WIRE_SIZE], frame[NIKON_WIRE_SIZE];

    begin_tx(t.q, 0, 0x32, false);
    wire_request(&t, 0, NIKON_I2C_START, 0x55, old);
    write_reg(t.q, 0, 0, 0x46);
    wire_read(&t, 0, frame);
    g_assert_cmpuint(frame[NIKON_WIRE_TYPE], ==, NIKON_WIRE_CANCEL);
    g_assert_cmphex(ldl_le_p(frame + NIKON_WIRE_LINE), ==, NIKON_I2C_START);
    g_assert_cmphex(ldq_le_p(frame + NIKON_WIRE_TOKEN), ==,
                    ldq_le_p(old + NIKON_WIRE_TOKEN));
    wire_reply(&t, 0, old, 0);
    begin_tx(t.q, 0, 0x33, false);
    wire_request(&t, 0, NIKON_I2C_START, 0x55, frame);
    g_assert_cmphex(ldq_le_p(frame + NIKON_WIRE_TOKEN), !=,
                    ldq_le_p(old + NIKON_WIRE_TOKEN));
    wire_reply(&t, 0, frame, 0);
    wire_request(&t, 0, NIKON_I2C_WRITE, 0x33, old);
    qtest_qmp_assert_success(t.q, "{'execute':'system_reset'}");
    wire_read(&t, 0, frame);
    g_assert_cmpuint(frame[NIKON_WIRE_TYPE], ==, NIKON_WIRE_CANCEL);
    g_assert_cmphex(ldl_le_p(frame + NIKON_WIRE_LINE), ==, NIKON_I2C_WRITE);
    wire_reply(&t, 0, old, 0);
    wire_quiet(&t, 0);
    g_assert_cmphex(read_reg(t.q, 0, 4) & 0x160, ==, 0);
    g_assert_cmphex(read_reg(t.q, 0, 0x10), ==, 0);
    begin_tx(t.q, 0, 0x34, false);
    wire_request(&t, 0, NIKON_I2C_START, 0x55, frame);
    wire_reply(&t, 0, frame, 0);
    wire_request(&t, 0, NIKON_I2C_WRITE, 0x34, frame);
    wire_reply(&t, 0, frame, 0);
    stop_reply(&t, 0);
    g_assert_cmphex(read_reg(t.q, 0, 0x10) & 1, ==, 1);
    wire_quit(&t);
}

static void test_fifo(void)
{
    I2CWireTest t = wire_start(1);
    uint8_t frame[NIKON_WIRE_SIZE];

    write_reg(t.q, 0, 0, 7);
    write_reg(t.q, 0, 0x14, 18);
    write_reg(t.q, 0, 8, 0x55);
    wire_request(&t, 0, NIKON_I2C_START, 0xd5, frame);
    wire_reply(&t, 0, frame, 0);
    for (unsigned i = 0; i < 16; i++) {
        wire_request(&t, 0, NIKON_I2C_READ, 0, frame);
        wire_reply(&t, 0, frame, i);
    }
    wait_reg(t.q, 0, 0x14, 0xff, 2);
    wire_quiet(&t, 0);
    g_assert_cmphex(read_reg(t.q, 0, 0x10) & 3, ==, 2);
    g_assert_cmphex(read_reg(t.q, 0, 0xc), ==, 0);
    wire_request(&t, 0, NIKON_I2C_READ, 0, frame);
    wire_reply(&t, 0, frame, 16);
    wait_reg(t.q, 0, 0x14, 0xff, 1);
    wire_quiet(&t, 0);
    g_assert_cmphex(read_reg(t.q, 0, 0xc), ==, 1);
    wire_request(&t, 0, NIKON_I2C_READ, 0, frame);
    wire_reply(&t, 0, frame, 17);
    wire_request(&t, 0, NIKON_I2C_NACK, 0, frame);
    wire_reply(&t, 0, frame, 0);
    stop_reply(&t, 0);
    for (unsigned i = 2; i < 18; i++) {
        g_assert_cmphex(read_reg(t.q, 0, 0xc), ==, i);
    }
    g_assert_cmphex(read_reg(t.q, 0, 0x10) & 1, ==, 1);
    wire_quit(&t);
}

static void test_timeout(void)
{
    I2CWireTest t = wire_start(1);
    uint8_t frame[NIKON_WIRE_SIZE];

    write_reg(t.q, 0, 0, 0x16);
    write_reg(t.q, 0, 0x1c, 3);
    write_reg(t.q, 0, 8, 0x55);
    wire_request(&t, 0, NIKON_I2C_START, 0x55, frame);
    wire_reply(&t, 0, frame, 0);
    wire_request(&t, 0, NIKON_I2C_STOP, 0, frame);
    g_assert_cmphex(read_reg(t.q, 0, 0x10) & 9, ==, 8);
    g_assert_cmphex(read_reg(t.q, 0, 4) & 0x100, ==, 0x100);
    wire_reply(&t, 0, frame, 0);
    wait_reg(t.q, 0, 4, 0x100, 0);
    g_assert_cmphex(read_reg(t.q, 0, 0x10) & 9, ==, 8);
    wire_quit(&t);
}

static void test_offline(void)
{
    g_autoptr(GString) args = g_string_new(NULL);
    QTestState *q;

    g_string_append_printf(args, "-machine z7ii-mas-diagnostic -smp 1 "
                           "-bios %s -serial none -display none", bios);
    for (unsigned i = 0; i < 4; i++) {
        g_string_append_printf(args, " -device nikon-peer,id=wire%u"
            " -device nikon-i2c-peer,peer=/machine/peripheral/wire%u,"
            "i2c=/machine/i2c%u", i, i, i);
    }
    q = qtest_init(args->str);
    qtest_qmp_assert_success(q, "{'execute':'cont'}");
    for (unsigned i = 0; i < 4; i++) {
        begin_tx(q, i, 0x60 + i, false);
    }
    qtest_clock_step(q, STEP_NS * 100);
    for (unsigned i = 0; i < 4; i++) {
        g_assert_cmphex(read_reg(q, i, 4) & 0x160, ==, 0x140);
        g_assert_cmphex(read_reg(q, i, 0x10) & 5, ==, 0);
    }
    write_reg(q, 0, 0, 0x46);
    g_assert_cmphex(read_reg(q, 0, 4) & 0x160, ==, 0);
    for (unsigned i = 1; i < 4; i++) {
        g_assert_cmphex(read_reg(q, i, 4) & 0x160, ==, 0x140);
    }
    qtest_quit(q);
}

static void test_ports(void)
{
    I2CWireTest t = wire_start(2);
    uint8_t frame[2][NIKON_WIRE_SIZE];

    for (unsigned i = 0; i < 2; i++) {
        begin_tx(t.q, i, 0x70 + i, false);
        wire_request(&t, i, NIKON_I2C_START, 0x55, frame[i]);
    }
    wire_reply(&t, 1, frame[1], 0);
    wire_request(&t, 1, NIKON_I2C_WRITE, 0x71, frame[1]);
    wire_reply(&t, 1, frame[1], 0);
    stop_reply(&t, 1);
    g_assert_cmphex(read_reg(t.q, 0, 4) & 0x140, ==, 0x140);
    g_assert_cmphex(read_reg(t.q, 0, 0x10) & 5, ==, 0);
    g_assert_cmphex(read_reg(t.q, 1, 0x10) & 1, ==, 1);
    wire_reply(&t, 0, frame[0], 0);
    wire_request(&t, 0, NIKON_I2C_WRITE, 0x70, frame[0]);
    wire_reply(&t, 0, frame[0], 0);
    stop_reply(&t, 0);
    g_assert_cmphex(read_reg(t.q, 0, 0x10) & 1, ==, 1);
    wire_quit(&t);
}

static void test_local(void)
{
    QTestState *q = qtest_initf("-machine z7ii-mas-diagnostic -smp 1 "
        "-bios %s -serial none -display none -device at24c-eeprom,"
        "bus=nikon-i2c3,address=0x50,rom-size=256", bios);

    qtest_qmp_assert_success(q, "{'execute':'cont'}");
    write_reg(q, 0, 0, 6);
    write_reg(q, 0, 8, 0x55);
    wait_reg(q, 0, 4, 0x100, 0);
    g_assert_cmphex(read_reg(q, 0, 0x10) & 5, ==, 4);
    write_reg(q, 3, 0, 6);
    write_reg(q, 3, 0xc, 5);
    write_reg(q, 3, 0xc, 0xab);
    write_reg(q, 3, 0xc, 0xcd);
    write_reg(q, 3, 8, 0x50);
    wait_reg(q, 3, 4, 0x100, 0);
    g_assert_cmphex(read_reg(q, 3, 0x10) & 5, ==, 1);
    write_reg(q, 3, 0x10, IRQ_MASK);
    write_reg(q, 3, 0, 0x16);
    write_reg(q, 3, 0xc, 5);
    write_reg(q, 3, 8, 0x50);
    wait_reg(q, 3, 4, 0x40, 0);
    write_reg(q, 3, 0, 0x17);
    write_reg(q, 3, 0x14, 2);
    write_reg(q, 3, 8, 0x50);
    write_reg(q, 3, 0, 7);
    wait_reg(q, 3, 4, 0x100, 0);
    g_assert_cmphex(read_reg(q, 3, 0xc), ==, 0xab);
    g_assert_cmphex(read_reg(q, 3, 0xc), ==, 0xcd);
    qtest_quit(q);
}

static void test_strobes(void)
{
    QTestState *q = qtest_initf("-machine z7ii-mas-diagnostic -smp 1 "
        "-bios %s -serial none -display none", bios);

    for (unsigned i = 0; i < 4; i++) {
        g_assert_cmphex(read_reg(q, i, 0x20), ==, IRQ_MASK);
        write_reg(q, i, 0x24, 0x2e7);
        g_assert_cmphex(read_reg(q, i, 0x24), ==, 0);
        g_assert_cmphex(read_reg(q, i, 0x28), ==, 0);
        g_assert_cmphex(read_reg(q, i, 0x20), ==, 0x18);
        /* Actual SLA TX/RX helpers read/OR2/write IDR; only DATA is masked. */
        write_reg(q, i, 0x28, read_reg(q, i, 0x28) | 2);
        g_assert_cmphex(read_reg(q, i, 0x28), ==, 0);
        g_assert_cmphex(read_reg(q, i, 0x20), ==, 0x1a);
        g_assert_cmphex(read_reg(q, i, 0x10), ==, 0);
        write_reg(q, i, 0x24, 2);
        g_assert_cmphex(read_reg(q, i, 0x20), ==, 0x18);
        write_reg(q, i, 0x28, IRQ_MASK);
        g_assert_cmphex(read_reg(q, i, 0x20), ==, IRQ_MASK);
    }
    qtest_quit(q);
}

static void notification(I2CWireTest *t, bool high)
{
    uint8_t frame[NIKON_WIRE_SIZE] = { 0 };

    stl_le_p(frame, NIKON_WIRE_MAGIC);
    frame[NIKON_WIRE_TYPE] = NIKON_WIRE_SIGNAL;
    frame[NIKON_WIRE_KIND] = NIKON_PEER_GPIO;
    /* qtest clock_step requires positive advancement, even for a timestamp. */
    stq_le_p(frame + NIKON_WIRE_TIME, qtest_clock_step(t->q, 1));
    stl_le_p(frame + NIKON_WIRE_LINE, NIKON_TOUCH_NOTIFY);
    stl_le_p(frame + NIKON_WIRE_WORD, high);
    g_assert_cmpint(send(t->fd[0], frame, sizeof(frame), 0), ==, sizeof(frame));
}

static void test_gpio(void)
{
    I2CWireTest t = wire_start_gpio(1, true);
    GPollFD pfd = { .fd = t.fd[0], .events = G_IO_IN };
    uint8_t frame[NIKON_WIRE_SIZE];

    nikon_intercept_gic(t.q);
    g_assert_cmpint(g_poll(&pfd, 1, 0), ==, 0);
    /* Source AA is GPIO10[13]; input latch alone does not drive a wire. */
    qtest_writel(t.q, GPIO + 0x28, 0x20002000);
    g_assert_cmpint(g_poll(&pfd, 1, 0), ==, 0);
    qtest_writel(t.q, GPIO + 0x128, 0x2000);
    wire_frame(&t, 0, frame);
    g_assert_cmpuint(frame[NIKON_WIRE_TYPE], ==, NIKON_WIRE_SIGNAL);
    g_assert_cmpuint(frame[NIKON_WIRE_KIND], ==, NIKON_PEER_GPIO);
    g_assert_cmpuint(frame[NIKON_WIRE_BITS], ==, 0);
    g_assert_cmphex(ldq_le_p(frame + NIKON_WIRE_TOKEN), ==, 0);
    g_assert_cmphex(ldl_le_p(frame + NIKON_WIRE_LINE), ==, NIKON_TOUCH_CONTROL);
    g_assert_cmphex(ldl_le_p(frame + NIKON_WIRE_WORD), ==, 1);
    /* A real disconnect invalidates output publication. Reopening while this
     * source pad is an input/alternate function cannot replay its old HIGH. */
    close(t.fd[0]);
    qtest_writel(t.q, GPIO + 0x128, 0);
    qtest_writel(t.q, GPIO + 0x228, 0x2000);
    pfd.fd = t.listener[0];
    g_assert_cmpint(g_poll(&pfd, 1, 1000), ==, 1);
    t.fd[0] = accept(t.listener[0], NULL, NULL);
    g_assert_cmpint(t.fd[0], >=, 0);
    wire_quiet(&t, 0);
    qtest_writel(t.q, GPIO + 0x28, 0x20000000);
    wire_quiet(&t, 0);
    /* Source63 GPIO6[6] is actual EXIU15/IRQ80, driven by incoming wire. */
    qtest_writel(t.q, EXIU, 0x7fff);
    notification(&t, true);
    wait_value(t.q, GPIO + 0x18, 0x40, 0x40);
    g_assert_cmphex(qtest_readl(t.q, EXIU + 0xc) & 0x8000, ==, 0x8000);
    g_assert_true(qtest_get_irq(t.q, 80 - 32));
    notification(&t, false);
    wait_value(t.q, GPIO + 0x18, 0x40, 0);
    qtest_writel(t.q, EXIU + 0x10, 0x8000);
    g_assert_false(qtest_get_irq(t.q, 80 - 32));
    g_assert_cmphex(read_reg(t.q, 0, 0x10), ==, 0);
    wire_quit(&t);
}

static void set_realized(QTestState *q, unsigned link, bool realized)
{
    g_autofree char *path = g_strdup_printf("/machine/peripheral/link%u", link);

    qtest_qmp_assert_success(q, realized ?
        "{'execute':'qom-set','arguments':{'path':%s,"
        "'property':'realized','value':true}}" :
        "{'execute':'qom-set','arguments':{'path':%s,"
        "'property':'realized','value':false}}", path);
}

static void set_link(QTestState *q, unsigned link, const char *property,
                      const char *target)
{
    g_autofree char *path = g_strdup_printf("/machine/peripheral/link%u", link);

    qtest_qmp_assert_success(q,
        "{'execute':'qom-set','arguments':{'path':%s,'property':%s,'value':%s}}",
        path, property, target);
}

static void realize_rejected(QTestState *q, const char *reason)
{
    QDict *error = qtest_qmp_assert_failure_ref(q,
        "{'execute':'qom-set','arguments':{'path':'/machine/peripheral/link0',"
        "'property':'realized','value':true}}");

    /* libqtest returns the retained inner error dictionary, not the response. */
    g_assert_nonnull(strstr(qdict_get_str(error, "desc"), reason));
    qobject_unref(error);
}

static void startup_rejected(bool local)
{
    g_autoptr(GPtrArray) args = g_ptr_array_new();
    g_autofree char *errors = NULL;
    g_autoptr(GError) error = NULL;
    const char *common[] = {
        qtest_qemu_binary(NULL), "-machine", "z7ii-mas-diagnostic", "-smp", "1",
        "-bios", bios, "-serial", "none", "-display", "none",
        "-run-with", "exit-with-parent=on",
    };
    const char *devices[] = {
        "-device", local ? "at24c-eeprom,bus=nikon-i2c0,address=0x50,rom-size=256"
                         : "nikon-peer,id=wire1",
        "-device", "nikon-peer,id=wire0",
        "-device", "nikon-i2c-peer,peer=/machine/peripheral/wire0,i2c=/machine/i2c0",
    };
    int status;

    for (unsigned i = 0; i < ARRAY_SIZE(common); i++) {
        g_ptr_array_add(args, (void *)common[i]);
    }
    for (unsigned i = 0; i < ARRAY_SIZE(devices); i++) {
        g_ptr_array_add(args, (void *)devices[i]);
    }
    if (!local) {
        g_ptr_array_add(args, "-device");
        g_ptr_array_add(args,
            "nikon-i2c-peer,peer=/machine/peripheral/wire1,i2c=/machine/i2c0");
    }
    g_ptr_array_add(args, NULL);
    g_assert_true(g_spawn_sync(NULL, (char **)args->pdata, NULL, 0, NULL, NULL,
                               NULL, &errors, &status, &error));
    g_assert_no_error(error);
    g_assert_true(WIFEXITED(status));
    g_assert_cmpint(WEXITSTATUS(status), ==, 1);
    g_assert_nonnull(strstr(errors, local ? "local slave devices" :
                            "already has an asynchronous peer"));
}

static void test_configuration(void)
{
    if (!g_test_subprocess()) {
        /* Accepted configurations must not hang this negative startup test. */
        g_test_trap_subprocess(NULL, 5 * G_TIME_SPAN_SECOND, 0);
        g_test_trap_assert_passed();
        return;
    }
    startup_rejected(true);
    startup_rejected(false);
}

static void frontend_takeover(QTestState *q)
{
    /* A different live frontend must be able to claim the failed one's peer.
     * This proves rollback, rather than merely re-registering the same owner. */
    set_realized(q, 1, false);
    set_link(q, 1, "peer", "/machine/peripheral/wire0");
    set_realized(q, 1, true);
    set_realized(q, 1, false);
    set_link(q, 1, "peer", "/machine/peripheral/wire1");
    set_realized(q, 1, true);
}

static void test_rollback(void)
{
    QTestState *q = qtest_initf("-machine z7ii-mas-diagnostic -smp 1 "
        "-bios %s -serial none -display none "
        "-device at24c-eeprom,bus=nikon-i2c2,address=0x50,rom-size=256 "
        "-device nikon-peer,id=wire0 -device nikon-peer,id=wire1 "
        "-device nikon-i2c-peer,id=link0,peer=/machine/peripheral/wire0,"
        "i2c=/machine/i2c0 "
        "-device nikon-i2c-peer,id=link1,peer=/machine/peripheral/wire1,"
        "i2c=/machine/i2c1", bios);

    set_realized(q, 0, false);
    set_link(q, 0, "i2c", "/machine/i2c2");
    realize_rejected(q, "local slave devices");
    frontend_takeover(q);
    set_link(q, 0, "i2c", "/machine/i2c0");
    set_realized(q, 0, true);
    set_realized(q, 0, false);
    set_link(q, 0, "i2c", "/machine/i2c1");
    realize_rejected(q, "already has an asynchronous peer");
    frontend_takeover(q);
    set_link(q, 0, "i2c", "/machine/i2c0");
    set_realized(q, 0, true);
    for (unsigned i = 0; i < 3; i++) {
        g_assert_cmphex(read_reg(q, i, 4) & 0x100, ==, 0);
        g_assert_cmphex(read_reg(q, i, 0x10), ==, 0);
    }
    qtest_quit(q);
}

static void test_lifecycle(void)
{
    I2CWireTest t = wire_start(1);
    uint8_t old[NIKON_WIRE_SIZE], frame[NIKON_WIRE_SIZE];

    begin_tx(t.q, 0, 0x38, false);
    wire_request(&t, 0, NIKON_I2C_START, 0x55, old);
    set_realized(t.q, 0, false);
    wire_read(&t, 0, frame);
    g_assert_cmpuint(frame[NIKON_WIRE_TYPE], ==, NIKON_WIRE_CANCEL);
    g_assert_cmphex(ldq_le_p(frame + NIKON_WIRE_TOKEN), ==,
                    ldq_le_p(old + NIKON_WIRE_TOKEN));
    wire_reply(&t, 0, old, 0);
    realize_rejected(t.q, "active transaction");
    wire_quiet(&t, 0);
    g_assert_cmphex(read_reg(t.q, 0, 4) & 0x140, ==, 0x140);
    g_assert_cmphex(read_reg(t.q, 0, 0x10), ==, 0);
    qtest_qmp_assert_success(t.q, "{'execute':'system_reset'}");
    g_assert_cmphex(read_reg(t.q, 0, 4) & 0x160, ==, 0);
    set_realized(t.q, 0, true);
    begin_tx(t.q, 0, 0x39, false);
    wire_request(&t, 0, NIKON_I2C_START, 0x55, frame);
    g_assert_cmphex(ldq_le_p(frame + NIKON_WIRE_TOKEN), !=,
                    ldq_le_p(old + NIKON_WIRE_TOKEN));
    wire_reply(&t, 0, frame, 0);
    wire_request(&t, 0, NIKON_I2C_WRITE, 0x39, frame);
    wire_reply(&t, 0, frame, 0);
    stop_reply(&t, 0);
    g_assert_cmphex(read_reg(t.q, 0, 0x10) & 1, ==, 1);
    set_realized(t.q, 0, false);
    set_realized(t.q, 0, true);
    wire_quiet(&t, 0);
    wire_quit(&t);
}

static QDict *realize_attempt(QTestState *q, unsigned link)
{
    g_autofree char *path = g_strdup_printf("/machine/peripheral/link%u", link);

    return qtest_qmp(q,
        "{'execute':'qom-set','arguments':{'path':%s,"
        "'property':'realized','value':true}}", path);
}

static void incoming_rejected(QTestState *q, unsigned link)
{
    QDict *response = realize_attempt(q, link);

    g_assert_true(qdict_haskey(response, "error"));
    g_assert_nonnull(strstr(qdict_get_str(qdict_get_qdict(response, "error"),
                                        "desc"), "pending incoming request"));
    qobject_unref(response);
}

static void test_incoming(void)
{
    I2CWireTest t = wire_start(2);
    uint8_t incoming[NIKON_WIRE_SIZE] = { 0 }, frame[NIKON_WIRE_SIZE];
    int64_t deadline;
    bool pending = false, realized = false;

    set_realized(t.q, 0, false);
    set_link(t.q, 0, "i2c", "/machine/i2c1");
    stl_le_p(incoming, NIKON_WIRE_MAGIC);
    incoming[NIKON_WIRE_TYPE] = NIKON_WIRE_REQUEST;
    incoming[NIKON_WIRE_BITS] = 8;
    incoming[NIKON_WIRE_KIND] = NIKON_PEER_I2C;
    stq_le_p(incoming + NIKON_WIRE_TOKEN, 0x80000001);
    stl_le_p(incoming + NIKON_WIRE_LINE, NIKON_I2C_START);
    stl_le_p(incoming + NIKON_WIRE_WORD, 0x55);
    g_assert_cmpint(send(t.fd[0], incoming, sizeof(incoming), 0), ==,
                    sizeof(incoming));

    /* Controller1 is owned by link1, so every attempt must fail even before
     * the socket callback consumes REQUEST. Observe the real pending error
     * without a delay assumption or a test-only transport state query. */
    deadline = g_get_monotonic_time() + G_TIME_SPAN_SECOND;
    while (!pending) {
        QDict *response = realize_attempt(t.q, 0);
        const char *desc;

        g_assert_true(qdict_haskey(response, "error"));
        desc = qdict_get_str(qdict_get_qdict(response, "error"), "desc");
        pending = strstr(desc, "pending incoming request") != NULL;
        if (!pending) {
            g_assert_nonnull(strstr(desc, "already has an asynchronous peer"));
        }
        qobject_unref(response);
        g_assert_cmpint(g_get_monotonic_time(), <, deadline);
        qtest_clock_step(t.q, STEP_NS);
        g_usleep(1000);
    }

    /* An untouched controller must not be claimed by the rejected frontend.
     * A second owner must see the same pending request, rather than a leaked
     * state hook. Repeated failures must not cancel the sender's request. */
    set_link(t.q, 0, "i2c", "/machine/i2c2");
    incoming_rejected(t.q, 0);
    incoming_rejected(t.q, 0);
    set_realized(t.q, 1, false);
    set_link(t.q, 1, "peer", "/machine/peripheral/wire0");
    set_link(t.q, 1, "i2c", "/machine/i2c2");
    incoming_rejected(t.q, 1);
    wire_quiet(&t, 0);
    g_assert_cmphex(read_reg(t.q, 2, 4) & 0x160, ==, 0);
    g_assert_cmphex(read_reg(t.q, 2, 0x10), ==, 0);

    incoming[NIKON_WIRE_TYPE] = NIKON_WIRE_CANCEL;
    stl_le_p(incoming + NIKON_WIRE_WORD, 0);
    g_assert_cmpint(send(t.fd[0], incoming, sizeof(incoming), 0), ==,
                    sizeof(incoming));
    deadline = g_get_monotonic_time() + G_TIME_SPAN_SECOND;
    while (!realized) {
        QDict *response = realize_attempt(t.q, 1);

        realized = !qdict_haskey(response, "error");
        if (!realized) {
            g_assert_nonnull(strstr(qdict_get_str(
                qdict_get_qdict(response, "error"), "desc"),
                "pending incoming request"));
        }
        qobject_unref(response);
        g_assert_cmpint(g_get_monotonic_time(), <, deadline);
        qtest_clock_step(t.q, STEP_NS);
        g_usleep(1000);
    }
    wire_quiet(&t, 0);

    /* Different-owner binding performs a complete, actual socket exchange. */
    begin_tx(t.q, 2, 0x3a, false);
    wire_request(&t, 0, NIKON_I2C_START, 0x55, frame);
    wire_reply(&t, 0, frame, 0);
    wire_request(&t, 0, NIKON_I2C_WRITE, 0x3a, frame);
    wire_reply(&t, 0, frame, 0);
    wire_request(&t, 0, NIKON_I2C_STOP, 0, frame);
    g_assert_cmphex(read_reg(t.q, 2, 4) & 0x100, ==, 0x100);
    wire_reply(&t, 0, frame, 0);
    wait_reg(t.q, 2, 4, 0x100, 0);
    g_assert_cmphex(read_reg(t.q, 2, 0x10) & 1, ==, 1);
    wire_quit(&t);
}

void nikon_i2c_add_tests(const char *image)
{
    bios = image;
    qtest_add_func("/nikon/i2c/local", test_local);
    qtest_add_func("/nikon/i2c/source", test_source);
    qtest_add_func("/nikon/i2c/nak", test_nak);
    qtest_add_func("/nikon/i2c/cancel", test_cancel);
    qtest_add_func("/nikon/i2c/fifo", test_fifo);
    qtest_add_func("/nikon/i2c/timeout", test_timeout);
    qtest_add_func("/nikon/i2c/offline", test_offline);
    qtest_add_func("/nikon/i2c/ports", test_ports);
    qtest_add_func("/nikon/i2c/gpio", test_gpio);
    qtest_add_func("/nikon/i2c/strobes", test_strobes);
    qtest_add_func("/nikon/i2c/configuration", test_configuration);
    qtest_add_func("/nikon/i2c/rollback", test_rollback);
    qtest_add_func("/nikon/i2c/lifecycle", test_lifecycle);
    qtest_add_func("/nikon/i2c/incoming", test_incoming);
}

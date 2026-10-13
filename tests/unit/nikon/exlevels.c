/* Explicit machine inputs reach the private EX expander's real I2C registers.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "qemu/crc-ccitt.h"
#include "hw/core/irq.h"
#include "hw/nikon/ex.h"
#include "hw/nikon/sla.h"
#include "system/reset.h"
#include "system/runstate.h"
#include "system/system.h"

int (*qemu_main)(void);

static char *input_bundle(const char *directory)
{
    static const char *const names[] = {
        "ex1840_013000.bi", "_tpj01_v140.bin", "eg1840_mas_01700",
        "vr1840_010100.bi", "eg1840_sla_01700",
    };
    const size_t header = 208;
    const size_t total = header + 3 * 6 + 0x80000 + 2 +
                         NIKON_SLA_ROM_SIZE + 2 + 16;
    g_autofree uint8_t *data = g_malloc0(total);
    char *path = g_build_filename(directory, "bundle", NULL);
    size_t offset = header;

    memset(data, ' ', 32);
    stl_be_p(data + 32, ARRAY_SIZE(names));
    stl_be_p(data + 36, header);
    for (unsigned i = 0; i < ARRAY_SIZE(names); i++) {
        size_t length = i == 0 ? 0x80000 :
                       i == 4 ? NIKON_SLA_ROM_SIZE : 4;
        uint8_t *entry = data + 48 + i * 32;

        memcpy(entry, names[i], strlen(names[i]));
        stl_be_p(entry + 16, offset);
        stl_be_p(entry + 20, length + 2);
        if (i == 0) {
            /* Vector-only synthetic EX; the fixture executes no guest code. */
            stl_le_p(data + offset, 0x20040000);
            stl_le_p(data + offset + 4, 0x08000009);
        } else {
            stl_le_p(data + offset, 0xeafffffe);
        }
        stw_be_p(data + offset + length,
                 crc_ccitt_false(0, data + offset, length));
        offset += length + 2;
    }
    stw_be_p(data + offset, crc_ccitt_false(0, data, offset));
    g_assert_true(g_file_set_contents(path, (const char *)data, total, NULL));
    return path;
}

static void io_write(I2CBus *bus, uint8_t reg, uint8_t value)
{
    g_assert_cmpint(i2c_start_transfer(bus, 0x22, false), ==, 0);
    g_assert_cmpint(i2c_send(bus, reg), ==, 0);
    g_assert_cmpint(i2c_send(bus, value), ==, 0);
    i2c_end_transfer(bus);
}

static uint32_t io_levels(I2CBus *bus)
{
    uint32_t levels = 0;

    for (unsigned port = 0; port < 3; port++) {
        g_assert_cmpint(i2c_start_transfer(bus, 0x22, false), ==, 0);
        g_assert_cmpint(i2c_send(bus, 0x6c + port), ==, 0);
        g_assert_cmpint(i2c_start_transfer(bus, 0x22, true), ==, 0);
        levels |= (uint32_t)i2c_recv(bus) << (port * 8);
        i2c_nack(bus);
        i2c_end_transfer(bus);
    }
    return levels;
}

static void configured_levels(void)
{
    NikonEXState *ex = NIKON_EX(object_resolve_path("/machine/ex", NULL));
    DeviceState *io = nikon_ex_io(ex);
    I2CBus *bus = nikon_ex_i2c_bus(ex, 0);

    /* Pins 0/23 are held low; pin9 high; all others are disconnected. */
    g_assert_cmphex(io_levels(bus), ==, 0x7ffffe);
    for (unsigned port = 0; port < 3; port++) {
        io_write(bus, 0x50 + port, 0); /* Pull direction: down. */
        io_write(bus, 0x4c + port, 0xff); /* Enable pulls on every pad. */
    }
    /* A supplied high overrides the pull; omitted pins follow the pull. */
    g_assert_cmphex(io_levels(bus), ==, 1 << 9);
    qemu_system_reset(SHUTDOWN_CAUSE_HOST_QMP_SYSTEM_RESET);
    g_assert_cmphex(io_levels(bus), ==, 0x7ffffe);

    /* Physical peer reset rejects transactions while held, then resets only
     * its registers: the three actual external drive levels remain present.
     */
    qemu_set_irq(qdev_get_gpio_in_named(io, "reset", 0), 0);
    g_assert_cmpint(i2c_start_transfer(bus, 0x22, false), !=, 0);
    i2c_end_transfer(bus);
    qemu_set_irq(qdev_get_gpio_in_named(io, "reset", 0), 1);
    g_assert_cmphex(io_levels(bus), ==, 0x7ffffe);

    /* A later physical input change survives reset; machine configuration is
     * applied once at realization, not replayed as a reset-time override.
     */
    qemu_set_irq(qdev_get_gpio_in_named(io, "pad", 0), 1);
    qemu_set_irq(qdev_get_gpio_in_named(io, "pad", 9), -1);
    qemu_set_irq(qdev_get_gpio_in_named(io, "pad", 23), -1);
    qemu_system_reset(SHUTDOWN_CAUSE_HOST_QMP_SYSTEM_RESET);
    g_assert_cmphex(io_levels(bus), ==, 0xffffff);
    io_write(bus, 0x51, 0);
    io_write(bus, 0x4d, 2);
    g_assert_cmphex(io_levels(bus), ==, 0xfffdff);
}

int main(int argc, char **argv)
{
    g_autofree char *directory = g_dir_make_tmp("nikonXXXXXX", NULL);
    g_autofree char *socket = NULL;
    g_autofree char *chardev = NULL;
    g_autofree char *bundle = NULL;
    g_autofree char *machine = NULL;
    int result;

    g_assert_nonnull(directory);
    socket = g_build_filename(directory, "qtest", NULL);
    chardev = g_strdup_printf("unix:%s,server=on,wait=off", socket);
    bundle = input_bundle(directory);
    machine = g_strdup_printf("z7ii-mas-diagnostic,bundle=%s,"
                              "ex-io-levels=0:0;9:1;23:0", bundle);
    g_test_init(&argc, &argv, NULL);
    {
        char *qemu_argv[] = {
            argv[0], "-machine", machine, "-accel", "qtest", "-display", "none",
            "-nodefaults", "-qtest", chardev, "-smp", "cpus=2,maxcpus=5", NULL
        };

        qemu_init(G_N_ELEMENTS(qemu_argv) - 1, qemu_argv);
    }
    g_test_add_func("/nikon/ex/configured-io-levels", configured_levels);
    result = g_test_run();
    qemu_cleanup(result);
    unlink(bundle);
    unlink(socket);
    rmdir(directory);
    return result;
}

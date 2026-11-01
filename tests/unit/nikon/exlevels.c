/* Direct device inputs reach the private EX expander's real I2C registers.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "qemu/crc-ccitt.h"
#include "qemu/timer.h"
#include "qemu/rcu.h"
#include "qapi/error.h"
#include "hw/core/irq.h"
#include "hw/i2c/i2c.h"
#include "hw/nikon/ex.h"
#include "hw/nikon/expower.h"
#include "hw/nikon/sla.h"
#include "system/address-spaces.h"
#include "system/reset.h"
#include "system/runstate.h"
#include "system/system.h"
#include "system/cpu-timers.h"

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

static void controller_write(AddressSpace *as, hwaddr address, uint32_t value)
{
    uint8_t data[4];

    stl_le_p(data, value);
    g_assert_cmpint(address_space_write(as, address, MEMTXATTRS_UNSPECIFIED,
                                       data, sizeof(data)), ==, MEMTX_OK);
}

static uint32_t controller_read(AddressSpace *as, hwaddr address)
{
    uint8_t data[4];

    g_assert_cmpint(address_space_read(as, address, MEMTXATTRS_UNSPECIFIED,
                                      data, sizeof(data)), ==, MEMTX_OK);
    return ldl_le_p(data);
}

static MemTxResult dma_write_result(AddressSpace *as, hwaddr address,
                                    uint32_t value, unsigned size)
{
    uint8_t data[4];

    stl_le_p(data, value);
    return address_space_write(as, address, MEMTXATTRS_UNSPECIFIED, data, size);
}

static void dma_write_protection(void)
{
    /* ST RM0090, 10.5 (word accesses), 10.5.6 NDTR, 10.5.7 PAR and
     * 10.5.8 M0AR (EN=0 or the inactive double-buffer bank):
     * https://www.st.com.cn/resource/en/reference_manual/
     * rm0090-stm32f405415-stm32f407417-stm32f427437-and-stm32f429439-
     * advanced-armbased-32bit-mcus-stmicroelectronics.pdf
     * Only the single-buffer write protection is covered here. */
    NikonEXState *ex = NIKON_EX(object_resolve_path("/machine/ex", NULL));
    AddressSpace as;

    qemu_system_reset(SHUTDOWN_CAUSE_HOST_QMP_SYSTEM_RESET);
    address_space_init(&as, nikon_ex_memory(ex), "dma-write-protection");
    for (unsigned c = 0; c < 2; c++) {
        hwaddr bank = 0x40026000 + c * 0x400;

        for (unsigned n = 0; n < 8; n++) {
            hwaddr stream = bank + 0x10 + n * 0x18;

            controller_write(&as, stream + 4, 65535);
            controller_write(&as, stream + 8, 0x4001300c);
            controller_write(&as, stream + 12, 0x20002000);
            controller_write(&as, stream, 0x411); /* RX, MINC, TCIE, EN. */
            /* RM0090 10.5.6-.8: EN=1 makes NDTR, PAR and single-buffer
             * M0AR write-protected. A protected write is not a bus fault. */
            controller_write(&as, stream + 4, 7);
            controller_write(&as, stream + 8, 0x4001340c);
            controller_write(&as, stream + 12, 0x20003000);
            g_assert_cmpuint(controller_read(&as, stream + 4), ==, 65535);
            g_assert_cmphex(controller_read(&as, stream + 8), ==, 0x4001300c);
            g_assert_cmphex(controller_read(&as, stream + 12), ==, 0x20002000);
            g_assert_cmphex(controller_read(&as, stream), ==, 0x411);
            g_assert_cmphex(controller_read(&as, bank), ==, 0);
            g_assert_cmphex(controller_read(&as, bank + 4), ==, 0);
            /* Disabling explicitly restores all three programming paths. */
            controller_write(&as, stream, 0x410);
            controller_write(&as, stream + 4, 0);
            g_assert_cmpuint(controller_read(&as, stream + 4), ==, 0);
            controller_write(&as, stream + 4, 7);
            controller_write(&as, stream + 8, 0x4001340c);
            controller_write(&as, stream + 12, 0x20003000);
            g_assert_cmpuint(controller_read(&as, stream + 4), ==, 7);
            g_assert_cmphex(controller_read(&as, stream + 8), ==, 0x4001340c);
            g_assert_cmphex(controller_read(&as, stream + 12), ==, 0x20003000);
        }
        controller_write(&as, 0x40023810, 1U << (21 + c));
        controller_write(&as, 0x40023810, 0);
        for (unsigned n = 0; n < 8; n++) {
            for (unsigned reg = 0; reg < 6; reg++) {
                g_assert_cmphex(controller_read(&as, bank + 0x10 + n * 0x18 + reg * 4),
                                ==, reg == 5 ? 0x21 : 0);
            }
        }
    }
    qemu_system_reset(SHUTDOWN_CAUSE_HOST_QMP_SYSTEM_RESET);
    address_space_destroy(&as);
    drain_call_rcu();
}

static void dma_protection_boundaries(void)
{
    NikonEXState *ex = NIKON_EX(object_resolve_path("/machine/ex", NULL));
    AddressSpace as;
    uint8_t data[4];

    qemu_system_reset(SHUTDOWN_CAUSE_HOST_QMP_SYSTEM_RESET);
    address_space_init(&as, nikon_ex_memory(ex), "dma-protection-boundaries");
    for (unsigned c = 0; c < 2; c++) {
        hwaddr bank = 0x40026000 + c * 0x400;
        hwaddr stream = bank + 0x10;

        controller_write(&as, stream + 4, 7);
        for (unsigned size = 1; size <= 2; size++) {
            for (unsigned reg = 1; reg <= 3; reg++) {
                g_assert_cmpint(dma_write_result(&as, stream + reg * 4, 3, size),
                                ==, MEMTX_ERROR);
                g_assert_cmpint(address_space_read(&as, stream + reg * 4,
                                    MEMTXATTRS_UNSPECIFIED, data, size),
                                ==, MEMTX_ERROR);
            }
        }
        g_assert_cmpint(dma_write_result(&as, stream + 5, 3, 4), ==, MEMTX_ERROR);
        g_assert_cmpint(dma_write_result(&as, stream + 4, 0x10000, 4),
                        ==, MEMTX_ERROR); /* Reserved NDTR bits stay unsupported. */
        g_assert_cmpuint(controller_read(&as, stream + 4), ==, 7);
        g_assert_cmpint(dma_write_result(&as, bank + 0xd0, 3, 4), ==, MEMTX_ERROR);
        /* Double-buffer register updates and FCR changes while active remain
         * outside this single-buffer contract, rather than being discarded. */
        controller_write(&as, stream, 0x40001);
        g_assert_cmpint(dma_write_result(&as, stream + 12, 0x20003000, 4),
                        ==, MEMTX_ERROR);
        controller_write(&as, stream, 1);
        g_assert_cmpint(dma_write_result(&as, stream + 16, 0x20003000, 4),
                        ==, MEMTX_ERROR);
        g_assert_cmpint(dma_write_result(&as, stream + 20, 0x21, 4),
                        ==, MEMTX_ERROR);
        controller_write(&as, stream, 0);
    }
    qemu_system_reset(SHUTDOWN_CAUSE_HOST_QMP_SYSTEM_RESET);
    address_space_destroy(&as);
    drain_call_rcu();
}

static void dma_protected_transfer(void)
{
    NikonEXState *ex = NIKON_EX(object_resolve_path("/machine/ex", NULL));
    AddressSpace as;
    const hwaddr bank = 0x40026400, stream = bank + 0x10;
    uint8_t data[16];

    qemu_system_reset(SHUTDOWN_CAUSE_HOST_QMP_SYSTEM_RESET);
    address_space_init(&as, nikon_ex_memory(ex), "dma-protected-transfer");
    memset(data, 0xa5, sizeof(data));
    g_assert_cmpint(address_space_write(&as, 0x20002000,
                        MEMTXATTRS_UNSPECIFIED, data, sizeof(data)), ==, MEMTX_OK);
    memset(data, 0x5a, sizeof(data));
    g_assert_cmpint(address_space_write(&as, 0x20003000,
                        MEMTXATTRS_UNSPECIFIED, data, sizeof(data)), ==, MEMTX_OK);
    /* The normal external EX SPI clock supplies each actual DMA request. */
    controller_write(&as, 0x40013000, 0x249);
    controller_write(&as, 0x40013004, 1);
    controller_write(&as, stream + 4, 7);
    controller_write(&as, stream + 8, 0x4001300c);
    controller_write(&as, stream + 12, 0x20002000);
    controller_write(&as, stream, 0x419);
    nikon_ex_spi_transfer(ex, 0, 0x31);
    nikon_ex_spi_transfer(ex, 0, 0x32);
    g_assert_cmpuint(controller_read(&as, stream + 4), ==, 5);
    controller_write(&as, stream + 4, 1);
    controller_write(&as, stream + 8, 0x4001340c);
    controller_write(&as, stream + 12, 0x20003000);
    g_assert_cmpuint(controller_read(&as, stream + 4), ==, 5);
    g_assert_cmphex(controller_read(&as, stream + 12), ==, 0x20002002);
    g_assert_cmphex(controller_read(&as, bank), ==, 0);
    for (unsigned i = 2; i < 7; i++) {
        nikon_ex_spi_transfer(ex, 0, 0x31 + i);
    }
    g_assert_cmpuint(controller_read(&as, stream + 4), ==, 0);
    g_assert_cmphex(controller_read(&as, stream) & 1, ==, 0);
    g_assert_cmphex(controller_read(&as, bank) & 0x3d, ==, 0x30);
    g_assert_cmpint(address_space_read(&as, 0x20002000,
                        MEMTXATTRS_UNSPECIFIED, data, sizeof(data)), ==, MEMTX_OK);
    for (unsigned i = 0; i < sizeof(data); i++) {
        g_assert_cmphex(data[i], ==, i < 7 ? 0x31 + i : 0xa5);
    }
    g_assert_cmpint(address_space_read(&as, 0x20003000,
                        MEMTXATTRS_UNSPECIFIED, data, sizeof(data)), ==, MEMTX_OK);
    for (unsigned i = 0; i < sizeof(data); i++) {
        g_assert_cmphex(data[i], ==, 0x5a);
    }
    /* Once EN clears, a new length/address drives the next real byte. */
    controller_write(&as, bank + 8, 0x3d);
    controller_write(&as, stream + 4, 1);
    controller_write(&as, stream + 12, 0x20002008);
    controller_write(&as, stream, 0x419);
    nikon_ex_spi_transfer(ex, 0, 0x68);
    g_assert_cmpuint(controller_read(&as, stream + 4), ==, 0);
    g_assert_cmpint(address_space_read(&as, 0x20002008,
                        MEMTXATTRS_UNSPECIFIED, data, 1), ==, MEMTX_OK);
    g_assert_cmphex(data[0], ==, 0x68);
    controller_write(&as, bank + 8, 0x3d);
    controller_write(&as, stream + 4, 3);
    controller_write(&as, stream + 12, 0x20002009);
    controller_write(&as, stream, 0x419);
    nikon_ex_spi_transfer(ex, 0, 0x69);
    controller_write(&as, stream, 0x418);
    nikon_ex_spi_transfer(ex, 0, 0x6a);
    g_assert_cmpuint(controller_read(&as, stream + 4), ==, 2);
    g_assert_cmphex(controller_read(&as, stream + 12), ==, 0x2000200a);
    qemu_system_reset(SHUTDOWN_CAUSE_HOST_QMP_SYSTEM_RESET);
    g_assert_cmpuint(controller_read(&as, stream + 4), ==, 0);
    g_assert_cmpuint(controller_read(&as, stream + 8), ==, 0);
    g_assert_cmpuint(controller_read(&as, stream + 12), ==, 0);
    address_space_destroy(&as);
    drain_call_rcu();
}

static void receive_start(AddressSpace *as, I2CBus *bus, uint32_t control)
{
    const hwaddr base = 0x40005400;

    /* Real expander registers, with auto-increment across the three outputs. */
    io_write(bus, 4, 0x12);
    io_write(bus, 5, 0x34);
    io_write(bus, 6, 0x56);
    g_assert_cmpint(i2c_start_transfer(bus, 0x22, false), ==, 0);
    g_assert_cmpint(i2c_send(bus, 0x84), ==, 0);
    i2c_end_transfer(bus);
    controller_write(as, base, control | 0x101);
    controller_write(as, base + 0x10, 0x45);
    g_assert_cmphex(controller_read(as, base + 0x14) & 2, ==, 2);
    controller_read(as, base + 0x18); /* SR1/SR2 releases address stretching. */
}

static void i2c_elapsed_receive(void)
{
    NikonEXState *ex = NIKON_EX(object_resolve_path("/machine/ex", NULL));
    I2CBus *bus = nikon_ex_i2c_bus(ex, 0);
    AddressSpace as;
    const hwaddr base = 0x40005400;
    int64_t start;

    qemu_system_reset(SHUTDOWN_CAUSE_HOST_QMP_SYSTEM_RESET);
    address_space_init(&as, nikon_ex_memory(ex), "i2c-elapsed");
    vm_start();
    receive_start(&as, bus, 0x400);
    start = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    /* Deliberately omit main-loop dispatch, as in the original HAL poll. */
    cpus_set_virtual_clock(start + 89999);
    g_assert_cmphex(controller_read(&as, base + 0x14) & 0x44, ==, 0x40);
    cpus_set_virtual_clock(start + 90000);
    g_assert_cmphex(controller_read(&as, base + 0x14) & 0x44, ==, 0x44);
    g_assert_cmphex(controller_read(&as, base + 0x10), ==, 0x12);
    g_assert_cmphex(controller_read(&as, base + 0x10), ==, 0x34);
    controller_write(&as, base, 0x201);
    qemu_clock_run_timers(QEMU_CLOCK_VIRTUAL);
    vm_stop(RUN_STATE_PAUSED);
    qemu_system_reset(SHUTDOWN_CAUSE_HOST_QMP_SYSTEM_RESET);
    address_space_destroy(&as);
    drain_call_rcu();
}

static void i2c_elapsed_boundaries(void)
{
    NikonEXState *ex = NIKON_EX(object_resolve_path("/machine/ex", NULL));
    I2CBus *bus = nikon_ex_i2c_bus(ex, 0);
    AddressSpace as;
    const hwaddr base = 0x40005400;

    address_space_init(&as, nikon_ex_memory(ex), "i2c-elapsed-boundaries");
    vm_start();
    for (unsigned kind = 0; kind < 5; kind++) {
        qemu_system_reset(SHUTDOWN_CAUSE_HOST_QMP_SYSTEM_RESET);
        receive_start(&as, bus, kind == 0 ? 0x800 : 0x400);
        if (kind == 1) {
            controller_write(&as, base, 1); /* Cancel ACK before the deadline. */
        } else if (kind == 2) {
            controller_write(&as, base, 0x601); /* STOP with ACK retained. */
        } else if (kind == 3) {
            controller_write(&as, base, 0); /* Peripheral disabled. */
        } else if (kind == 4) {
            controller_write(&as, 0x40023820, 1 << 21); /* RCC reset. */
        }
        cpus_set_virtual_clock(qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 1000000);
        uint32_t status = controller_read(&as, base + 0x14);
        g_assert_cmphex(status & 0x44, ==, kind == 0 ? 0x44 :
                        kind < 3 ? 0x40 : 0);
        qemu_clock_run_timers(QEMU_CLOCK_VIRTUAL);
        if (kind == 0) {
            /* POS/ACK=0 clocks exactly two bytes, with no replayed callback. */
            g_assert_cmphex(controller_read(&as, base + 0x10), ==, 0x12);
            g_assert_cmphex(controller_read(&as, base + 0x10), ==, 0x34);
            g_assert_cmphex(controller_read(&as, base + 0x14) & 0x44, ==, 0);
            controller_write(&as, base, 0xa01);
        } else if (kind < 3) {
            g_assert_cmphex(controller_read(&as, base + 0x10), ==, 0x12);
            g_assert_cmphex(controller_read(&as, base + 0x14) & 0x44, ==, 0);
        }
    }
    vm_stop(RUN_STATE_PAUSED);
    qemu_system_reset(SHUTDOWN_CAUSE_HOST_QMP_SYSTEM_RESET);
    address_space_destroy(&as);
    drain_call_rcu();
}

static void configured_levels(void)
{
    NikonEXState *ex = NIKON_EX(object_resolve_path("/machine/ex", NULL));
    DeviceState *io = nikon_ex_io(ex);
    I2CBus *bus = nikon_ex_i2c_bus(ex, 0);

    /* The input frontend owns its released contacts. Pad17 is disconnected
     * and independently exercises external drive and guest pull rules.
     */
    qemu_set_irq(qdev_get_gpio_in_named(io, "pad", 17), 1);
    g_assert_cmphex(io_levels(bus), ==, 0xffffff);
    for (unsigned port = 0; port < 3; port++) {
        io_write(bus, 0x50 + port, 0); /* Pull direction: down. */
        io_write(bus, 0x4c + port, 0xff); /* Enable pulls on every pad. */
    }
    /* A supplied high overrides the pull; omitted pins follow the pull. */
    g_assert_cmphex(io_levels(bus), ==, 0xe2fbfd);
    qemu_system_reset(SHUTDOWN_CAUSE_HOST_QMP_SYSTEM_RESET);
    g_assert_cmphex(io_levels(bus), ==, 0xffffff);

    /* Physical peer reset rejects transactions while held, then resets only
     * its registers: the three actual external drive levels remain present.
     */
    qemu_set_irq(qdev_get_gpio_in_named(io, "reset", 0), 0);
    g_assert_cmpint(i2c_start_transfer(bus, 0x22, false), !=, 0);
    i2c_end_transfer(bus);
    qemu_set_irq(qdev_get_gpio_in_named(io, "reset", 0), 1);
    g_assert_cmphex(io_levels(bus), ==, 0xffffff);

    /* A later physical input change survives reset; machine configuration is
     * applied once at realization, not replayed as a reset-time override.
     */
    qemu_set_irq(qdev_get_gpio_in_named(io, "pad", 17), 0);
    qemu_system_reset(SHUTDOWN_CAUSE_HOST_QMP_SYSTEM_RESET);
    g_assert_cmphex(io_levels(bus), ==, 0xfdffff);
    qemu_set_irq(qdev_get_gpio_in_named(io, "pad", 17), -1);
    qemu_system_reset(SHUTDOWN_CAUSE_HOST_QMP_SYSTEM_RESET);
    g_assert_cmphex(io_levels(bus), ==, 0xffffff);
    io_write(bus, 0x52, 0);
    io_write(bus, 0x4e, 2);
    g_assert_cmphex(io_levels(bus), ==, 0xfdffff);
}

static uint8_t monitor_read(I2CBus *bus, uint8_t command)
{
    uint8_t value;

    g_assert_cmpint(i2c_start_transfer(bus, 0x6b, false), ==, 0);
    g_assert_cmpint(i2c_send(bus, command), ==, 0);
    g_assert_cmpint(i2c_start_transfer(bus, 0x6b, true), ==, 0);
    value = i2c_recv(bus);
    i2c_nack(bus);
    i2c_end_transfer(bus);
    return value;
}

static uint8_t monitor_write_read(I2CBus *bus, uint8_t command, uint8_t value)
{
    uint8_t result;

    g_assert_cmpint(i2c_start_transfer(bus, 0x6b, false), ==, 0);
    g_assert_cmpint(i2c_send(bus, command), ==, 0);
    g_assert_cmpint(i2c_send(bus, value), ==, 0);
    g_assert_cmpint(i2c_start_transfer(bus, 0x6b, true), ==, 0);
    result = i2c_recv(bus);
    i2c_nack(bus);
    i2c_end_transfer(bus);
    return result;
}

static void status_read(I2CBus *bus, uint8_t value[4])
{
    g_assert_cmpint(i2c_start_transfer(bus, 0x0d, false), ==, 0);
    g_assert_cmpint(i2c_send(bus, 0x08), ==, 0);
    g_assert_cmpint(i2c_send(bus, 0x01), ==, 0);
    g_assert_cmpint(i2c_start_transfer(bus, 0x0d, true), ==, 0);
    for (unsigned i = 0; i < 4; i++) {
        value[i] = i2c_recv(bus);
    }
    i2c_nack(bus);
    i2c_end_transfer(bus);
}

static void power_devices(void)
{
    NikonEXState *ex = NIKON_EX(object_resolve_path("/machine/ex", NULL));
    I2CBus *bus = nikon_ex_i2c_bus(ex, 0);

    /* The boot qualification writes each setup register and reads it back.
     * A zero-valued register therefore reads as zero. */
    g_assert_cmphex(monitor_read(bus, 0x00), ==, 0x00);
    g_assert_cmphex(monitor_write_read(bus, 0x00, 0xa0), ==, 0xa0);
    g_assert_cmphex(monitor_write_read(bus, 0x01, 0xc0), ==, 0xc0);
    g_assert_cmphex(monitor_write_read(bus, 0x02, 0x00), ==, 0x00);
    g_assert_cmphex(monitor_write_read(bus, 0x03, 0x1f), ==, 0x1f);

    /* The final 0x0b result is a separate ready status: one, then zero for
     * controller prefetch, rather than an echoed setup register. */
    g_assert_cmpint(i2c_start_transfer(bus, 0x6b, false), ==, 0);
    g_assert_cmpint(i2c_send(bus, 0x0b), ==, 0);
    g_assert_cmpint(i2c_start_transfer(bus, 0x6b, true), ==, 0);
    g_assert_cmphex(i2c_recv(bus), ==, 1);
    g_assert_cmphex(i2c_recv(bus), ==, 0);
    i2c_nack(bus);
    i2c_end_transfer(bus);

    /* The original commit marshals this payload: cmd0 a0, cmd1 c0, cmd2 a0,
     * cmd3 00, cmd4 00, cmd5 8d, cmd6 33 and cmd7 80.  Commands 0, 1 and 3
     * are read back under the firmware masks; the rest are write-only. */
    g_assert_cmphex(monitor_write_read(bus, 0x00, 0xa0), ==, 0xa0);
    g_assert_cmphex(monitor_write_read(bus, 0x01, 0xc0), ==, 0xc0);
    g_assert_cmphex(monitor_write_read(bus, 0x03, 0x00), ==, 0x00);
    {
        static const struct {
            uint8_t command;
            uint8_t value;
        } write_only[] = {
            { 0x02, 0xa0 }, { 0x04, 0x00 }, { 0x05, 0x8d },
            { 0x06, 0x33 }, { 0x07, 0x80 },
        };

        for (unsigned i = 0; i < ARRAY_SIZE(write_only); i++) {
            g_assert_cmpint(i2c_start_transfer(bus, 0x6b, false), ==, 0);
            g_assert_cmpint(i2c_send(bus, write_only[i].command), ==, 0);
            g_assert_cmpint(i2c_send(bus, write_only[i].value), ==, 0);
            i2c_end_transfer(bus);
        }
    }

    /* The three-failure sanitiser writes cmd1 = c0 and expects success. */
    g_assert_cmphex(monitor_write_read(bus, 0x01, 0xc0), ==, 0xc0);
    /* Status reads the lane depends on: 0x10 must keep bit7 clear and 0x11
     * must not request a reprogram. */
    g_assert_cmphex(monitor_read(bus, 0x10), ==, 0x00);
    g_assert_cmphex(monitor_read(bus, 0x11), ==, 0x00);

    /* A command beyond the register file is NACKed. */
    g_assert_cmpint(i2c_start_transfer(bus, 0x6b, false), ==, 0);
    g_assert_cmpint(i2c_send(bus, 0x12), !=, 0);
    i2c_end_transfer(bus);

    /* Reset clears the setup registers; the ready status remains one. */
    qemu_system_reset(SHUTDOWN_CAUSE_HOST_QMP_SYSTEM_RESET);
    g_assert_cmphex(monitor_read(bus, 0x00), ==, 0x00);
    g_assert_cmphex(monitor_read(bus, 0x0b), ==, 0x01);

    /* The periodic status endpoint returns a finite four-byte response whose
     * fourth byte is the explicit class register; the leading bytes are
     * reserved.  The transfer descriptor reports completion code 0x20
     * separately. */
    Object *status = object_resolve_path("/machine/ex/status", NULL);
    uint8_t value[4];

    g_assert_nonnull(status);
    status_read(bus, value);
    g_assert_cmphex(value[0], ==, 0);
    g_assert_cmphex(value[1], ==, 0);
    g_assert_cmphex(value[2], ==, 0);
    g_assert_cmphex(value[3], ==, 0xa9); /* default class 1 */

    object_property_set_uint(status, "class", 0, &error_abort);
    status_read(bus, value);
    g_assert_cmphex(value[3], ==, 0x00);
    object_property_set_uint(status, "class", 2, &error_abort);
    status_read(bus, value);
    g_assert_cmphex(value[3], ==, 0xc9);
    object_property_set_uint(status, "class", 3, &error_abort);
    status_read(bus, value);
    g_assert_cmphex(value[3], ==, 0xe9);

    /* A configured class is a board input; reset preserves it. */
    object_property_set_uint(status, "class", 2, &error_abort);
    qemu_system_reset(SHUTDOWN_CAUSE_HOST_QMP_SYSTEM_RESET);
    status_read(bus, value);
    g_assert_cmphex(value[3], ==, 0xc9);
    object_property_set_uint(status, "class", 1, &error_abort);
    status_read(bus, value);
    g_assert_cmphex(value[3], ==, 0xa9);
}

static uint32_t status_pin(AddressSpace *as)
{
    uint8_t data[4];

    /* Native synthetic MCU setup: GPIOC0 is an input, as in the EX driver. */
    stl_le_p(data, 0);
    g_assert_cmpint(address_space_write(as, 0x40020800,
        MEMTXATTRS_UNSPECIFIED, data, 4), ==, MEMTX_OK);
    g_assert_cmpint(address_space_read(as, 0x40020810,
        MEMTXATTRS_UNSPECIFIED, data, 4), ==, MEMTX_OK);
    return ldl_le_p(data) & 1;
}

static void status_release(I2CBus *bus, uint8_t token)
{
    g_assert_cmpint(i2c_start_transfer(bus, 0x0d, false), ==, 0);
    g_assert_cmpint(i2c_send(bus, token), ==, 0);
    g_assert_cmpint(i2c_send(bus, 0), ==, 0);
    i2c_end_transfer(bus);
}

static void status_notification(void)
{
    NikonEXState *ex = NIKON_EX(object_resolve_path("/machine/ex", NULL));
    Object *status = object_resolve_path("/machine/ex/status", NULL);
    I2CBus *bus = nikon_ex_i2c_bus(ex, 0);
    AddressSpace as;
    uint8_t value[4];
    Error *error = NULL;

    address_space_init(&as, nikon_ex_memory(ex), "status-notification");
    qemu_system_reset(SHUTDOWN_CAUSE_HOST_QMP_SYSTEM_RESET);
    g_assert_cmpuint(status_pin(&as), ==, 1);
    object_property_set_uint(status, "class", 2, &error_abort);
    g_assert_cmpuint(status_pin(&as), ==, 0);

    /* A short or interrupted acquisition cannot acknowledge a changed input. */
    g_assert_cmpint(i2c_start_transfer(bus, 0x0d, false), ==, 0);
    g_assert_cmpint(i2c_send(bus, 8), ==, 0);
    g_assert_cmpint(i2c_send(bus, 1), ==, 0);
    g_assert_cmpint(i2c_start_transfer(bus, 0x0d, true), ==, 0);
    for (unsigned i = 0; i < 3; i++) {
        i2c_recv(bus);
    }
    i2c_nack(bus);
    i2c_end_transfer(bus);
    status_release(bus, 0);
    g_assert_cmpuint(status_pin(&as), ==, 0);
    status_read(bus, value);
    g_assert_cmphex(value[3], ==, 0xc9);
    g_assert_cmpuint(status_pin(&as), ==, 0);
    g_assert_cmpint(i2c_start_transfer(bus, 0x0d, false), ==, 0);
    g_assert_cmpint(i2c_send(bus, value[0]), ==, 0);
    i2c_end_transfer(bus); /* Missing release's second byte. */
    g_assert_cmpuint(status_pin(&as), ==, 0);
    status_release(bus, value[0]);
    g_assert_cmpuint(status_pin(&as), ==, 1);
    object_property_set_uint(status, "class", 2, &error_abort);
    g_assert_cmpuint(status_pin(&as), ==, 1);

    /* Freeze the entire response even if input changes during its receive.
     * An old acquisition, including controller prefetch, cannot consume that
     * later change.
     */
    object_property_set_uint(status, "class", 1, &error_abort);
    g_assert_cmpint(i2c_start_transfer(bus, 0x0d, false), ==, 0);
    g_assert_cmpint(i2c_send(bus, 8), ==, 0);
    g_assert_cmpint(i2c_send(bus, 1), ==, 0);
    g_assert_cmpint(i2c_start_transfer(bus, 0x0d, true), ==, 0);
    value[0] = i2c_recv(bus);
    g_assert_cmphex(i2c_recv(bus), ==, 0);
    object_property_set_uint(status, "class", 3, &error_abort);
    g_assert_cmphex(i2c_recv(bus), ==, 0);
    g_assert_cmphex(i2c_recv(bus), ==, 0xa9);
    g_assert_cmphex(i2c_recv(bus), ==, 0); /* Controller prefetch. */
    i2c_nack(bus);
    i2c_end_transfer(bus);
    status_release(bus, value[0]);
    g_assert_cmpuint(status_pin(&as), ==, 0);
    status_read(bus, value);
    g_assert_cmphex(value[3], ==, 0xe9);
    status_release(bus, value[0]);
    g_assert_cmpuint(status_pin(&as), ==, 1);

    /* Class 0 is a real input, not absence of a sample. */
    object_property_set_uint(status, "class", 0, &error_abort);
    g_assert_cmpuint(status_pin(&as), ==, 0);
    status_read(bus, value);
    g_assert_cmphex(value[3], ==, 0);
    status_release(bus, value[0]);
    g_assert_cmpuint(status_pin(&as), ==, 1);

    object_property_set_uint(status, "class", 4, &error);
    g_assert_nonnull(error);
    error_free(error);
    g_assert_cmpuint(object_property_get_uint(status, "class", &error_abort),
                     ==, 0);
    g_assert_cmpuint(status_pin(&as), ==, 1);
    g_assert_cmpint(i2c_start_transfer(bus, 0x0d, false), ==, 0);
    g_assert_cmpint(i2c_send(bus, 0xa9), !=, 0);
    i2c_end_transfer(bus);

    object_property_set_uint(status, "class", 2, &error_abort);
    status_read(bus, value);
    qemu_system_reset(SHUTDOWN_CAUSE_HOST_QMP_SYSTEM_RESET);
    g_assert_cmpuint(status_pin(&as), ==, 1);
    g_assert_cmpuint(object_property_get_uint(status, "class", &error_abort),
                     ==, 2);
    status_release(bus, value[0]);
    g_assert_cmpuint(status_pin(&as), ==, 1);
    address_space_destroy(&as);
}

static void status_interleaved_lifecycle(void)
{
    NikonEXState *ex = NIKON_EX(object_resolve_path("/machine/ex", NULL));
    Object *status = object_resolve_path("/machine/ex/status", NULL);
    Object *input = object_resolve_path("/machine/input", NULL);
    I2CBus *bus = nikon_ex_i2c_bus(ex, 0);
    AddressSpace as;
    uint8_t value[4];

    g_assert_nonnull(input);
    g_assert_nonnull(status);
    address_space_init(&as, nikon_ex_memory(ex), "status-interleaved");
    qemu_system_reset(SHUTDOWN_CAUSE_HOST_QMP_SYSTEM_RESET);

    /* 1. Baseline: Ensure starting class is 1, drained and PC0 idle high. */
    object_property_set_uint(status, "class", 1, &error_abort);
    status_read(bus, value);
    status_release(bus, value[0]);
    g_assert_cmpuint(status_pin(&as), ==, 1);

    /* 2. Interleave: Battery class transition to Class 2 asserts PC0. */
    object_property_set_uint(status, "class", 2, &error_abort);
    g_assert_cmpuint(status_pin(&as), ==, 0);

    /* 3. Interleave: Mode change during pending battery notification. */
    object_property_set_int(input, "mode-position", 0, &error_abort);
    g_assert_cmpint(object_property_get_int(input, "mode-position",
                                            &error_abort), ==, 0);

    /* 4. Interleave: Shutter half-press and AF-ON assertions. */
    object_property_set_bool(input, "half-press", true, &error_abort);
    object_property_set_bool(input, "af-on", true, &error_abort);
    g_assert_true(object_property_get_bool(input, "half-press", &error_abort));
    g_assert_true(object_property_get_bool(input, "af-on", &error_abort));

    /* 5. Complete I2C battery acquisition while inputs are active. */
    status_read(bus, value);
    g_assert_cmphex(value[3], ==, 0xc9);

    /* Freeze response test: input changes during read retain old generation. */
    object_property_set_uint(status, "class", 3, &error_abort);
    status_release(bus, value[0]);
    /* PC0 remains asserted because Class 3 is pending. */
    g_assert_cmpuint(status_pin(&as), ==, 0);

    /* Second acquisition consumes Class 3 and returns PC0 high. */
    status_read(bus, value);
    g_assert_cmphex(value[3], ==, 0xe9);
    status_release(bus, value[0]);
    g_assert_cmpuint(status_pin(&as), ==, 1);

    /* 6. System Reset retains held keys and preserves battery class. */
    qemu_system_reset(SHUTDOWN_CAUSE_HOST_QMP_SYSTEM_RESET);
    g_assert_true(object_property_get_bool(input, "half-press", &error_abort));
    g_assert_true(object_property_get_bool(input, "af-on", &error_abort));
    g_assert_cmpuint(object_property_get_uint(status, "class", &error_abort),
                     ==, 3);
    g_assert_cmpuint(status_pin(&as), ==, 1);

    /* Clean release. */
    object_property_set_bool(input, "half-press", false, &error_abort);
    object_property_set_bool(input, "af-on", false, &error_abort);
    object_property_set_uint(status, "class", 1, &error_abort);
    status_read(bus, value);
    status_release(bus, value[0]);
    g_assert_cmpuint(status_pin(&as), ==, 1);

    address_space_destroy(&as);
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
    machine = g_strdup_printf("z7ii-mas-diagnostic,bundle=%s", bundle);
    g_test_init(&argc, &argv, NULL);
    {
        char *qemu_argv[] = {
            argv[0], "-machine", machine, "-accel", "qtest", "-display", "none",
            "-nodefaults", "-qtest", chardev, "-smp", "cpus=2,maxcpus=5",
            "-S", NULL
        };

        qemu_init(G_N_ELEMENTS(qemu_argv) - 1, qemu_argv);
    }
    g_test_add_func("/nikon/ex/configured-io-levels", configured_levels);
    g_test_add_func("/nikon/ex/i2c-elapsed-receive", i2c_elapsed_receive);
    g_test_add_func("/nikon/ex/i2c-elapsed-boundaries", i2c_elapsed_boundaries);
    g_test_add_func("/nikon/ex/dma-write-protection", dma_write_protection);
    g_test_add_func("/nikon/ex/dma-protection-boundaries", dma_protection_boundaries);
    g_test_add_func("/nikon/ex/dma-protected-transfer", dma_protected_transfer);
    g_test_add_func("/nikon/ex/power-devices", power_devices);
    g_test_add_func("/nikon/ex/status-notification", status_notification);
    g_test_add_func("/nikon/ex/status-interleaved-lifecycle",
                    status_interleaved_lifecycle);
    result = g_test_run();
    qemu_cleanup(result);
    unlink(bundle);
    unlink(socket);
    rmdir(directory);
    return result;
}

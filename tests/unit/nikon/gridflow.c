/* Actual gate MMIO, RGB DMA ownership and synchronous publication regressions.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/bswap.h"
#include "qemu/timer.h"
#include "hw/core/irq.h"
#include "hw/core/sysbus.h"
#include "hw/nikon/devices.h"
#include "hw/nikon/gateplane.h"
#include "hw/nikon/gridinput.h"
#include "system/address-spaces.h"
#include "system/cpu-timers.h"
#include "system/runstate.h"
#include "system/system.h"
#include "gridfixture.h"

int (*qemu_main)(void);

#define GATE 0x20100000
#define TABLE 0x24000000
#define WORKSPACE 0x24010000

typedef enum ReadyAction {
    READY_RECORD,
    READY_ACK,
    READY_RESET,
} ReadyAction;

typedef struct Flow {
    Fixture *pixels;
    DeviceState *dev;
    qemu_irq irq, ready;
    NikonPairedGridFrame publication;
    unsigned fresh, withdrawn;
    bool level;
    ReadyAction action;
    uint64_t reset_completed, reset_phase;
    uint8_t expected[HEIGHT * 32];
} Flow;

static void write_word(hwaddr address, uint32_t value)
{
    uint8_t data[4];

    stl_le_p(data, value);
    g_assert_cmpint(address_space_write(&address_space_memory, address,
                       MEMTXATTRS_UNSPECIFIED, data, sizeof(data)), ==, MEMTX_OK);
}

static uint32_t read_word(hwaddr address)
{
    uint8_t data[4];

    g_assert_cmpint(address_space_read(&address_space_memory, address,
                       MEMTXATTRS_UNSPECIFIED, data, sizeof(data)), ==, MEMTX_OK);
    return ldl_le_p(data);
}

static uint64_t count(Flow *f, const char *name)
{
    return object_property_get_uint(OBJECT(f->dev), name, &error_abort);
}

static void advance(int64_t ns)
{
    cpus_set_virtual_clock(qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + ns);
    qemu_clock_run_timers(QEMU_CLOCK_VIRTUAL);
}

static void ack(void)
{
    write_word(GATE + 0x1c, 0x10);
    write_word(GATE + 0x20, 0x44);
}

static void irq_level(void *opaque, int n, int level)
{
    ((Flow *)opaque)->level = !!level;
}

static void ready_level(void *opaque, int n, int level)
{
    Flow *f = opaque;

    if (!level) {
        return;
    }
    g_assert_true(nikon_gate_plane_output(f->dev, &f->publication));
    if (!f->publication.generation) {
        f->withdrawn++;
        return;
    }
    f->fresh++;
    /* The result's backpressure must precede this synchronous callback. */
    g_assert_cmphex(read_word(GATE + 0x1c) & 0x10, ==, 0x10);
    g_assert_cmphex(read_word(GATE + 0x20) & 4, ==, 4);
    if (f->action == READY_ACK) {
        ack();
    } else if (f->action == READY_RESET) {
        device_cold_reset(f->dev);
        f->reset_completed = count(f, "grid-completed");
        f->reset_phase = count(f, "grid-phase");
    }
}

/* Fixture fields already describe the parsed source layout. Contiguous present
 * runs become command blocks; round-trip decode verifies the real wire packet.
 */
static void import_packet(Flow *f, unsigned index, unsigned kind)
{
    const NikonGatePacket *packet = &f->pixels->packet[index];
    NikonGatePacket decoded;
    uint8_t bytes[NIKON_GATE_PACKET_MAX];
    size_t length = 0;
    uint32_t address = BASE + 0x17000 + index * 0x200;

    for (unsigned i = 0; i < NIKON_GATE_REGISTERS;) {
        unsigned start, words;
        bool more = false;

        if (!packet->present[i]) {
            i++;
            continue;
        }
        start = i;
        while (i < NIKON_GATE_REGISTERS && packet->present[i]) {
            i++;
        }
        words = i - start + 1;
        for (unsigned j = i; j < NIKON_GATE_REGISTERS; j++) {
            more |= packet->present[j];
        }
        g_assert_cmpuint(length + words * 4, <=, sizeof(bytes));
        stl_le_p(bytes + length, start * 4 | words << 16 |
                 (more ? 0x11000000 : 0x01000000));
        for (unsigned j = start; j < i; j++) {
            stl_le_p(bytes + length + 4 * (j - start + 1), packet->value[j]);
        }
        length += words * 4;
    }
    g_assert_cmpuint(length, ==, nikon_gate_packet_size(kind));
    g_assert_true(nikon_gate_packet_decode(kind, bytes, length, &decoded));
    for (unsigned i = 0; i < NIKON_GATE_REGISTERS; i++) {
        g_assert_cmpint(decoded.present[i], ==, packet->present[i]);
        if (packet->present[i]) {
            g_assert_cmphex(decoded.value[i], ==, packet->value[i]);
        }
    }
    g_assert_cmpint(address_space_write(&address_space_memory, address,
                       MEMTXATTRS_UNSPECIFIED, bytes, length), ==, MEMTX_OK);
    unsigned port = f->pixels->command[index].offset;

    if (kind == 10) {
        write_word(GATE + port + 8, f->pixels->command[index].auxiliary[0]);
        write_word(GATE + port + 12, f->pixels->command[index].auxiliary[1]);
    }
    write_word(GATE + port + 0x14, 1);
    write_word(GATE + port + 0x14, 0);
    write_word(GATE + port + 0x10, address);
}

static void configure(Flow *f)
{
    static const unsigned kinds[] = { 4, 15, 12, 17, 17, 11, 7, 10 };
    static const unsigned controls[] = {
        0x2000, 0x2004, 0x2008, 0x2010, 0x2014, 0x2018,
        0x2640, 0x2644, 0x2648, 0x264c, 0x2650, 0x2658, 0x4040, 0x4044,
    };
    Fixture *p = f->pixels;

    for (unsigned bank = 0; bank < 2; bank++) {
        for (unsigned phase = 0; phase < 4; phase++) {
            select_phase(p, bank, phase);
        }
    }
    select_phase(p, 0, 0);
    for (unsigned slot = 1; slot <= 2; slot++) {
        for (unsigned i = 0; i < 13; i++) {
            write_word(GATE + 0x400 + slot * 0x40 + i * 4, p->slots[slot][i]);
        }
        write_word(GATE + 0x2c, slot);
    }
    for (unsigned i = 0; i < G_N_ELEMENTS(kinds); i++) {
        import_packet(f, i, kinds[i]);
    }
    for (unsigned i = 0; i < G_N_ELEMENTS(controls); i++) {
        write_word(GATE + controls[i], p->config[controls[i] / 4]);
    }
    for (unsigned offset = 0x2200; offset < 0x2400; offset += 4) {
        write_word(GATE + offset, p->config[offset / 4]);
    }
    for (unsigned bank = 0; bank < 6; bank++) {
        for (unsigned i = 0; i < 1024; i++) {
            write_word(TABLE + bank * 0x1000 + i * 4, p->table[bank][i]);
        }
    }
    for (unsigned i = 0; i < G_N_ELEMENTS(kinds); i++) {
        write_word(GATE + p->command[i].offset, 1);
    }
    write_word(GATE + 0x10, 0x10);
    write_word(GATE + 0x14, 0x44);
    write_word(GATE + 4, 1);
}

static Flow *flow_new(void)
{
    Flow *f = g_new0(Flow, 1);
    NikonGatePlaneOutput out;

    f->pixels = fixture();
    /* The wrapper has independent scalar coverage in gateplane.c. Here its
     * output is the numerical oracle for runtime scheduling and publication. */
    g_assert_cmpint(nikon_gate_plane_execute(&f->pixels->work, &out), ==,
                    NIKON_GATE_WORK_OK);
    memcpy(f->expected, f->pixels->bytes + out.address - BASE, sizeof(f->expected));
    memset(f->pixels->bytes + 0x8000, 0xa5, 0x8000);
    f->dev = qdev_new(TYPE_NIKON_EVENT_GATE);
    f->irq = qemu_allocate_irq(irq_level, f, 0);
    f->ready = qemu_allocate_irq(ready_level, f, 0);
    sysbus_connect_irq(SYS_BUS_DEVICE(f->dev), 0, f->irq);
    qdev_connect_gpio_out_named(f->dev, "grid-ready", 0, f->ready);
    g_assert_true(sysbus_realize(SYS_BUS_DEVICE(f->dev), &error_abort));
    sysbus_mmio_map(SYS_BUS_DEVICE(f->dev), 0, GATE);
    sysbus_mmio_map(SYS_BUS_DEVICE(f->dev), 1, TABLE);
    sysbus_mmio_map(SYS_BUS_DEVICE(f->dev), 2, WORKSPACE);
    device_cold_reset(f->dev);
    configure(f);
    vm_start();
    return f;
}

static void flow_free(Flow *f)
{
    f->action = READY_RECORD;
    qdev_unrealize(f->dev);
    object_unparent(OBJECT(f->dev));
    object_unref(OBJECT(f->dev));
    qemu_free_irq(f->irq);
    qemu_free_irq(f->ready);
    cleanup(f->pixels);
    g_free(f);
}

static NikonGatePlaneInput metadata(unsigned phase, uint64_t generation)
{
    NikonGatePlaneInput input = {
        .generation = generation, .width = WIDTH, .height = HEIGHT,
    };

    for (unsigned c = 0; c < 3; c++) {
        input.address[c] = BASE + 0x1000 + phase * 0x200 + c * 0x2000;
        input.stride[c] = 32;
    }
    return input;
}

static void produce(Flow *f, unsigned phase, uint64_t generation)
{
    NikonGatePlaneInput input = metadata(phase, generation);
    uint8_t row[WIDTH * 2];

    for (unsigned c = 0; c < 3; c++) {
        for (unsigned y = 0; y < HEIGHT; y++) {
            for (unsigned x = 0; x < WIDTH; x++) {
                stw_le_p(row + x * 2,
                         f->pixels->rgb[c * SAMPLES + y * WIDTH + x]);
            }
            g_assert_cmpint(address_space_write(&address_space_memory,
                input.address[c] + y * input.stride[c], MEMTXATTRS_UNSPECIFIED,
                row, sizeof(row)), ==, MEMTX_OK);
        }
    }
    g_assert_true(nikon_gate_plane_input(f->dev, &input));
}

static void untouched(Flow *f)
{
    for (unsigned i = 0x8000; i < 0x10000; i++) {
        g_assert_cmphex(f->pixels->bytes[i], ==, 0xa5);
    }
    g_assert_cmpuint(count(f, "grid-completed"), ==, 0);
    g_assert_cmphex(read_word(GATE + 0x1c), ==, 0);
    g_assert_cmphex(read_word(GATE + 0x20), ==, 0);
    g_assert_cmpuint(f->fresh, ==, 0);
}

static void result(Flow *f, unsigned bank, unsigned phase)
{
    uint32_t address = BASE + 0x8000 + phase * 0x1000 + bank * 0x4000;

    g_assert_cmpmem(f->pixels->bytes + address - BASE, sizeof(f->expected),
                    f->expected, sizeof(f->expected));
    g_assert_cmpuint(f->publication.address, ==, address);
    g_assert_cmpuint(f->publication.width, ==, WIDTH);
    g_assert_cmpuint(f->publication.height, ==, HEIGHT);
    g_assert_cmpuint(f->publication.stride, ==, 32);
    g_assert_cmpuint(count(f, "grid-errors"), ==, 0);
}

static void test_dma_latency(void)
{
    Flow *f = flow_new();
    NikonGatePlaneInput input = metadata(0, 0);
    unsigned withdrawn = f->withdrawn;

    g_assert_true(nikon_gate_plane_input(f->dev, &input));
    advance(2000000);
    untouched(f);
    produce(f, 0, 1);
    advance(500000);
    /* Independent MAIN setup must not restart this grid's latency. */
    write_word(GATE + 0x2100, 0);
    write_word(GATE + 0x2104, 0xc2);
    advance(499999);
    untouched(f);
    advance(1);
    result(f, 0, 0);
    g_assert_cmpuint(f->withdrawn, ==, withdrawn + 1);
    g_assert_cmpuint(count(f, "grid-completed"), ==, 1);
    g_assert_cmphex(read_word(GATE + 0x1c), ==, 0x10);
    g_assert_cmphex(read_word(GATE + 0x20), ==, 4);
    g_assert_true(f->level);
    g_assert_cmpuint(count(f, "grid-cancelled"), ==, 0);
    flow_free(f);
}

static void test_backpressure(void)
{
    Flow *f = flow_new();
    NikonGatePlaneInput replay = metadata(1, 2);

    produce(f, 0, 1);
    produce(f, 1, 2);
    advance(1000000);
    result(f, 0, 0);
    advance(2000000);
    g_assert_cmpuint(count(f, "grid-completed"), ==, 1);
    write_word(GATE + 0x1c, 0x10);
    advance(2000000);
    g_assert_cmpuint(count(f, "grid-completed"), ==, 1);
    write_word(GATE + 0x20, 4);
    advance(999999);
    g_assert_cmpuint(count(f, "grid-completed"), ==, 1);
    advance(1);
    result(f, 0, 1);
    g_assert_cmpuint(count(f, "grid-completed"), ==, 2);
    ack();
    write_word(GATE + 0x2004, 0xd2);
    g_assert_true(nikon_gate_plane_input(f->dev, &replay));
    advance(2000000);
    g_assert_cmpuint(count(f, "grid-completed"), ==, 2);
    flow_free(f);
}

static void test_input_stop(void)
{
    Flow *f = flow_new();
    NikonGatePlaneInput next = metadata(1, 2);
    NikonPairedGridFrame previous, held;
    uint8_t word[4];
    unsigned fresh, withdrawn;

    produce(f, 0, 1);
    stl_le_p(word, 1);
    g_assert_cmpint(address_space_write(&address_space_memory, GATE + 0x20a4,
        MEMTXATTRS_UNSPECIFIED, word, 4), ==, MEMTX_ERROR);
    advance(1000000);
    result(f, 0, 0);
    produce(f, 1, 2);
    g_assert_true(nikon_gate_plane_output(f->dev, &previous));
    fresh = f->fresh;
    withdrawn = f->withdrawn;
    write_word(GATE + 0x2000, 0);
    write_word(GATE + 0x2648, 0);
    write_word(GATE + 0x20a4, 1);
    g_assert_false(nikon_gate_plane_input(f->dev, &next));
    advance(2000000);
    g_assert_true(nikon_gate_plane_output(f->dev, &held));
    g_assert_cmpuint(held.generation, ==, previous.generation);
    g_assert_cmphex(held.address, ==, previous.address);
    g_assert_cmpuint(f->fresh, ==, fresh);
    g_assert_cmpuint(f->withdrawn, ==, withdrawn);
    g_assert_cmpuint(count(f, "grid-completed"), ==, 1);
    g_assert_cmpuint(count(f, "grid-cancelled"), ==, 0);
    g_assert_cmphex(read_word(GATE + 0x1c), ==, 0x10);
    g_assert_cmphex(read_word(GATE + 0x20), ==, 4);
    g_assert_true(f->level);
    for (unsigned offset = 0x2200; offset < 0x2400; offset += 4) {
        g_assert_cmphex(read_word(GATE + offset), ==,
                        f->pixels->config[offset / 4]);
    }
    for (unsigned bank = 0; bank < 6; bank++) {
        for (unsigned i = 0; i < 1024; i++) {
            g_assert_cmphex(read_word(TABLE + bank * 0x1000 + i * 4), ==,
                            f->pixels->table[bank][i]);
        }
    }
    write_word(GATE + 0x20a4, 0);
    write_word(GATE + 0x2004, 0xd2);
    write_word(GATE + 0x2000, f->pixels->config[0x2000 / 4]);
    write_word(GATE + 0x2648, f->pixels->config[0x2648 / 4]);
    advance(2000000);
    g_assert_cmpuint(count(f, "grid-completed"), ==, 1);
    ack();
    advance(1000000);
    result(f, 0, 1);
    g_assert_cmpuint(count(f, "grid-completed"), ==, 2);
    flow_free(f);
}

static void test_maps(void)
{
    Flow *f = flow_new();

    for (unsigned phase = 0; phase < 4; phase++) {
        produce(f, phase, phase + 1);
        advance(1000000);
        result(f, 0, phase);
        g_assert_cmphex(read_word(GATE + 0x20), ==, phase == 3 ? 0x44 : 4);
        ack();
    }
    write_word(GATE + 0x2004, 0xc3);
    produce(f, 0, 5);
    advance(1000000);
    result(f, 1, 0);
    g_assert_cmpuint(count(f, "grid-completed"), ==, 5);
    flow_free(f);
}

static void test_cancel(gconstpointer opaque)
{
    Flow *f = flow_new();
    bool reset = GPOINTER_TO_INT(opaque);

    produce(f, 0, 1);
    if (reset) {
        device_cold_reset(f->dev);
    } else {
        write_word(GATE + 4, 0);
    }
    advance(2000000);
    untouched(f);
    if (reset) {
        configure(f);
    } else {
        write_word(GATE + 4, 1);
    }
    advance(2000000);
    untouched(f);
    produce(f, 0, 2);
    advance(1000000);
    result(f, 0, 0);
    flow_free(f);
}

static void test_withdrawal(void)
{
    Flow *f = flow_new();
    NikonGatePlaneInput input = metadata(0, 0);

    produce(f, 0, 1);
    g_assert_true(nikon_gate_plane_input(f->dev, &input));
    /* The scheduled job owns its RGB copy; no RAM reread may replace it. */
    for (unsigned c = 0; c < 3; c++) {
        memset(f->pixels->bytes + input.address[c] - BASE, 0, HEIGHT * 32);
    }
    advance(1000000);
    result(f, 0, 0);
    ack();
    write_word(GATE + 0x2004, 0xd2);
    write_word(GATE + 0x2004, 0xc2);
    advance(2000000);
    g_assert_cmpuint(count(f, "grid-completed"), ==, 1);
    produce(f, 0, 2);
    advance(1000000);
    result(f, 0, 0);
    g_assert_cmpuint(count(f, "grid-completed"), ==, 2);
    flow_free(f);
}

static void test_reentrant(gconstpointer opaque)
{
    Flow *f = flow_new();

    f->action = GPOINTER_TO_INT(opaque);
    produce(f, 0, 1);
    produce(f, 1, 2);
    advance(1000000);
    g_assert_cmpuint(f->fresh, ==, 1);
    g_assert_cmphex(read_word(GATE + 0x1c), ==, 0);
    g_assert_cmphex(read_word(GATE + 0x20), ==, 0);
    g_assert_false(f->level);
    if (f->action == READY_RESET) {
        g_assert_cmpuint(count(f, "grid-completed"), ==, f->reset_completed);
        g_assert_cmpuint(count(f, "grid-phase"), ==, f->reset_phase);
        advance(2000000);
        g_assert_cmpuint(f->fresh, ==, 1);
        g_assert_cmpuint(count(f, "grid-completed"), ==, 0);
        for (unsigned i = 0x9000; i < 0x9000 + HEIGHT * 32; i++) {
            g_assert_cmphex(f->pixels->bytes[i], ==, 0xa5);
        }
    } else {
        result(f, 0, 0);
        g_assert_cmpuint(count(f, "grid-phase"), ==, 1);
        advance(999999);
        g_assert_cmpuint(f->fresh, ==, 1);
        advance(1);
        result(f, 0, 1);
        g_assert_cmpuint(count(f, "grid-completed"), ==, 2);
        g_assert_cmpuint(count(f, "grid-phase"), ==, 2);
    }
    flow_free(f);
}

int main(int argc, char **argv)
{
    g_autofree char *directory = g_dir_make_tmp("nikonXXXXXX", NULL);
    g_autofree char *socket = g_build_filename(directory, "qtest", NULL);
    g_autofree char *chardev = g_strdup_printf("unix:%s,server=on,wait=off", socket);
    const char *arguments[] = {
        argv[0], "-machine", "none", "-accel", "qtest", "-display", "none",
        "-nodefaults", "-S", "-qtest", chardev, NULL,
    };
    g_auto(GStrv) mutable_argv = g_new0(char *, G_N_ELEMENTS(arguments));
    int status;

    g_assert_nonnull(directory);
    g_test_init(&argc, &argv, NULL);
    for (unsigned i = 0; i + 1 < G_N_ELEMENTS(arguments); i++) {
        mutable_argv[i] = g_strdup(arguments[i]);
    }
    qemu_init(G_N_ELEMENTS(arguments) - 1, mutable_argv);
    g_test_add_func("/nikon/gridflow/dma-latency", test_dma_latency);
    g_test_add_func("/nikon/gridflow/w1c-backpressure", test_backpressure);
    g_test_add_func("/nikon/gridflow/input-stop", test_input_stop);
    g_test_add_func("/nikon/gridflow/four-phases-map-bank", test_maps);
    g_test_add_data_func("/nikon/gridflow/reset-before-dma", GINT_TO_POINTER(1),
                         test_cancel);
    g_test_add_data_func("/nikon/gridflow/stop-before-dma", GINT_TO_POINTER(0),
                         test_cancel);
    g_test_add_func("/nikon/gridflow/owned-withdrawal", test_withdrawal);
    g_test_add_data_func("/nikon/gridflow/synchronous-ack", GINT_TO_POINTER(READY_ACK),
                         test_reentrant);
    g_test_add_data_func("/nikon/gridflow/synchronous-reset", GINT_TO_POINTER(READY_RESET),
                         test_reentrant);
    status = g_test_run();
    qemu_cleanup(status);
    unlink(socket);
    rmdir(directory);
    return status;
}

/* Real CMOS chunk deadlines under delayed native timer dispatch.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/rcu.h"
#include "qemu/timer.h"
#include "hw/core/sysbus.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-properties.h"
#include "hw/nikon/cmos.h"
#include "system/address-spaces.h"
#include "system/cpu-timers.h"
#include "system/runstate.h"
#include "system/system.h"

#define RAM_BASE 0x40000000u
#define RAM_SIZE 0x10000u
#define WIDTH 2056u
#define ROW_BYTES (WIDTH * 2)
#define STRIDE (ROW_BYTES + 16)
#define HEIGHT 3u
#define EXPOSURE 7u
#define READOUT 11u
#define DURATION (EXPOSURE + HEIGHT * 2 * READOUT)

int (*qemu_main)(void);

typedef struct CmosFixture {
    DeviceState *dev;
    MemoryRegion ram, *regs;
    qemu_irq observed[4];
    unsigned pulses[4];
    bool level[4], restart;
    uint64_t second_token;
    NikonCmosSubmission next;
} CmosFixture;

static void observe(void *opaque, int n, int level)
{
    CmosFixture *f = opaque;

    f->level[n] = !!level;
    f->pulses[n] += !!level;
    if (n == 1 && level && f->restart) {
        f->restart = false;
        f->second_token = nikon_cmos_submit(f->dev, &f->next, &error_abort);
        g_assert_cmpuint(f->second_token, >, 0);
    }
}

static CmosFixture *fixture(void)
{
    CmosFixture *f = g_new0(CmosFixture, 1);

    f->dev = qdev_new(TYPE_NIKON_CMOS);
    qdev_prop_set_uint32(f->dev, "red-response", 2198);
    qdev_prop_set_uint32(f->dev, "blue-response", 2966);
    object_property_add_child(object_get_objects_root(), "cmos[*]", OBJECT(f->dev));
    object_property_set_bool(OBJECT(f->dev), "fixture-provider", true, &error_abort);
    memory_region_init_ram_flags_nomigrate(&f->ram, OBJECT(f->dev),
                                          "cmos.test-ram", RAM_SIZE, 0,
                                          &error_abort);
    memory_region_add_subregion(get_system_memory(), RAM_BASE, &f->ram);
    g_assert_true(sysbus_realize(SYS_BUS_DEVICE(f->dev), &error_abort));
    for (unsigned i = 0; i < ARRAY_SIZE(f->observed); i++) {
        f->observed[i] = qemu_allocate_irq(observe, f, i);
        if (!i) {
            sysbus_connect_irq(SYS_BUS_DEVICE(f->dev), 0, f->observed[i]);
        } else {
            qdev_connect_gpio_out_named(f->dev, "pipeline-event", i - 1,
                                        f->observed[i]);
        }
    }
    device_cold_reset(f->dev);
    f->regs = sysbus_mmio_get_region(SYS_BUS_DEVICE(f->dev), 0);
    memset(memory_region_get_ram_ptr(&f->ram), 0xa5, RAM_SIZE);
    return f;
}

static void cleanup(CmosFixture *f)
{
    qdev_unrealize(f->dev);
    memory_region_del_subregion(get_system_memory(), &f->ram);
    drain_call_rcu();
    object_unparent(OBJECT(f->dev));
    object_unref(OBJECT(f->dev));
    for (unsigned i = 0; i < ARRAY_SIZE(f->observed); i++) {
        qemu_free_irq(f->observed[i]);
    }
    drain_call_rcu();
    g_free(f);
}

static void write_word(CmosFixture *f, unsigned offset, uint32_t value)
{
    g_assert_cmpint(memory_region_dispatch_write(f->regs, offset, value, MO_32,
                                                MEMTXATTRS_UNSPECIFIED),
                    ==, MEMTX_OK);
}

static uint32_t read_word(CmosFixture *f, unsigned offset)
{
    uint64_t value;

    g_assert_cmpint(memory_region_dispatch_read(f->regs, offset, &value, MO_32,
                                               MEMTXATTRS_UNSPECIFIED),
                    ==, MEMTX_OK);
    return value;
}

static NikonCmosSubmission submission(void)
{
    return (NikonCmosSubmission) {
        .width = WIDTH, .height = HEIGHT, .bytes_per_pixel = 2,
        .stride = STRIDE, .destination = RAM_BASE,
        .format = NIKON_CMOS_FORMAT_FUNCTIONAL_C170,
        .transport_seed = 0x12345678,
        .exposure_ns = EXPOSURE, .readout_ns = READOUT,
    };
}

static void dispatch_at(int64_t now)
{
    cpus_set_virtual_clock(now);
    qemu_clock_run_timers(QEMU_CLOCK_VIRTUAL);
}

static void verify(CmosFixture *f)
{
    const uint8_t *data = memory_region_get_ram_ptr(&f->ram);

    for (unsigned y = 0; y < HEIGHT; y++) {
        for (unsigned byte = 0; byte < ROW_BYTES; byte++) {
            unsigned x = byte / 2, channel = byte % 2;
            uint32_t value = 0x12345678u + x * 0x45d9f3bu;

            value ^= y * 0x27d4eb2du;
            value += channel * 0x9e3779b9u;
            value ^= (x + y) * 0x7f4a7c15u;
            g_assert_cmphex(data[y * STRIDE + byte], ==,
                           (uint8_t)(value >> (channel * 8)));
        }
        for (unsigned byte = ROW_BYTES; byte < STRIDE; byte++) {
            g_assert_cmphex(data[y * STRIDE + byte], ==, 0xa5);
        }
    }
    g_assert_cmphex(data[HEIGHT * STRIDE], ==, 0xa5);
}

static void test_late_dma(void)
{
    CmosFixture *f = fixture();
    NikonCmosSubmission request = submission();
    uint8_t fine[HEIGHT * STRIDE];
    int64_t start = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    uint64_t token = nikon_cmos_submit(f->dev, &request, &error_abort);

    write_word(f, 0x6c, NIKON_CMOS_EVENT_DONE);
    for (unsigned ns = 1; ns <= DURATION; ns++) {
        dispatch_at(start + ns);
    }
    g_assert_true(nikon_cmos_completed(f->dev, token));
    verify(f);
    memcpy(fine, memory_region_get_ram_ptr(&f->ram), sizeof(fine));
    cleanup(f);

    f = fixture();
    start = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    token = nikon_cmos_submit(f->dev, &request, &error_abort);
    write_word(f, 0x6c, NIKON_CMOS_EVENT_DONE);
    /* Skip exposure and all six row-bounded chunk deadlines in one dispatch. */
    dispatch_at(start + 100000);
    g_assert_cmpmem(memory_region_get_ram_ptr(&f->ram), sizeof(fine),
                    fine, sizeof(fine));
    verify(f);
    g_assert_true(nikon_cmos_completed(f->dev, token));
    g_assert_cmpuint(read_word(f, 0x68), ==, NIKON_CMOS_DONE);
    g_assert_cmpuint(read_word(f, 0x78), ==, 1);
    g_assert_true(f->level[0]);
    g_assert_cmpuint(f->pulses[1], ==, 1);
    g_assert_cmpuint(f->pulses[2], ==, 0);
    write_word(f, 0x74, NIKON_CMOS_EVENT_DONE);
    g_assert_false(f->level[0]);
    cleanup(f);
}

static void test_cancellation(void)
{
    for (unsigned reset = 0; reset < 2; reset++) {
        CmosFixture *f = fixture();
        NikonCmosSubmission request = submission();
        uint8_t partial[HEIGHT * STRIDE];
        int64_t start = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        uint64_t token = nikon_cmos_submit(f->dev, &request, &error_abort);

        dispatch_at(start + EXPOSURE);
        dispatch_at(start + EXPOSURE + READOUT);
        memcpy(partial, memory_region_get_ram_ptr(&f->ram), sizeof(partial));
        g_assert_cmphex(partial[0], ==, 0x78);
        g_assert_cmphex(partial[4096], ==, 0xa5);
        if (reset) {
            device_cold_reset(f->dev);
        } else {
            nikon_cmos_cancel(f->dev);
        }
        dispatch_at(start + 100000);
        g_assert_false(nikon_cmos_completed(f->dev, token));
        g_assert_cmpmem(memory_region_get_ram_ptr(&f->ram), sizeof(partial),
                        partial, sizeof(partial));
        g_assert_cmpuint(f->pulses[1], ==, 0);
        g_assert_cmpuint(f->pulses[3], ==, !reset);

        memset(memory_region_get_ram_ptr(&f->ram), 0xa5, RAM_SIZE);
        start = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        token = nikon_cmos_submit(f->dev, &request, &error_abort);
        dispatch_at(start);
        dispatch_at(start + EXPOSURE - 1);
        g_assert_cmphex(((uint8_t *)memory_region_get_ram_ptr(&f->ram))[0],
                       ==, 0xa5);
        g_assert_cmpuint(read_word(f, 0x68), ==, NIKON_CMOS_EXPOSING);
        dispatch_at(start + 100000);
        g_assert_true(nikon_cmos_completed(f->dev, token));
        cleanup(f);
    }
}

static void test_reentrant(void)
{
    CmosFixture *f = fixture();
    NikonCmosSubmission request = submission();
    int64_t start = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

    f->next = request;
    f->next.destination += 0x8000;
    f->restart = true;
    nikon_cmos_submit(f->dev, &request, &error_abort);
    dispatch_at(start + 100000);
    verify(f);
    g_assert_cmpuint(f->pulses[1], ==, 1);
    g_assert_cmpuint(f->second_token, >, 0);
    g_assert_false(nikon_cmos_completed(f->dev, f->second_token));
    g_assert_cmphex(((uint8_t *)memory_region_get_ram_ptr(&f->ram))[0x8000],
                   ==, 0xa5);
    g_assert_cmpuint(read_word(f, 0x68), ==, NIKON_CMOS_EXPOSING);
    dispatch_at(start + 200000);
    g_assert_true(nikon_cmos_completed(f->dev, f->second_token));
    g_assert_cmpuint(f->pulses[1], ==, 2);
    cleanup(f);
}

static void test_overflow(void)
{
    CmosFixture *f = fixture();
    NikonCmosSubmission request = submission();
    int64_t start = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

    request.readout_ns = INT64_MAX;
    uint64_t token = nikon_cmos_submit(f->dev, &request, &error_abort);

    dispatch_at(start + 100000);
    g_assert_false(nikon_cmos_completed(f->dev, token));
    g_assert_cmpuint(read_word(f, 0x68), ==, NIKON_CMOS_ERROR);
    g_assert_cmphex(((uint8_t *)memory_region_get_ram_ptr(&f->ram))[0], ==, 0xa5);
    g_assert_cmpuint(f->pulses[1], ==, 0);
    g_assert_cmpuint(f->pulses[2], ==, 1);
    cleanup(f);
}

static void test_period_anchor(void)
{
    CmosFixture *f = fixture();
    NikonCmosSubmission request = submission();
    int64_t start = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

    /* Retain the complete transport configuration, then declare its period. */
    g_assert_cmpuint(nikon_cmos_submit(f->dev, &request, &error_abort), >, 0);
    nikon_cmos_cancel(f->dev);
    write_word(f, 0x50, 1000);
    write_word(f, 0x60, 1);
    dispatch_at(start + 100000);
    verify(f);
    g_assert_cmpuint(read_word(f, 0x68), ==, NIKON_CMOS_DONE);
    g_assert_cmpuint(f->pulses[1], ==, 1);

    /* Scheduled DONE plus period is already past; late dispatch must not
     * create a new cooldown. The next acquisition still starts at fresh now. */
    memset(memory_region_get_ram_ptr(&f->ram), 0xa5, RAM_SIZE);
    write_word(f, 0x60, 1);
    g_assert_cmpuint(read_word(f, 0x68), ==, NIKON_CMOS_EXPOSING);
    dispatch_at(start + 100000 + EXPOSURE - 1);
    g_assert_cmphex(((uint8_t *)memory_region_get_ram_ptr(&f->ram))[0], ==, 0xa5);
    g_assert_cmpuint(f->pulses[1], ==, 1);
    cleanup(f);
}

#define CFA_WIDTH 2058u
#define CFA_HEIGHT 4u
#define CFA_STRIDE (CFA_WIDTH * 2 + 16)

static char *cfa_image(unsigned width, unsigned height, unsigned variant)
{
    g_autofree char *header = g_strdup_printf("P6\n%u %u\n255\n", width, height);
    size_t prefix = strlen(header), size = prefix + width * height * 3;
    g_autofree uint8_t *data = g_malloc(size);
    char *path = NULL;
    int fd = g_file_open_tmp("nikon-cfa-coordinates-XXXXXX", &path, NULL);

    g_assert_cmpint(fd, >=, 0);
    close(fd);
    memcpy(data, header, prefix);
    for (unsigned y = 0; y < height; y++) {
        for (unsigned x = 0; x < width; x++) {
            for (unsigned channel = 0; channel < 3; channel++) {
                data[prefix + 3 * (y * width + x) + channel] =
                    37 * x + 19 * y + 73 * channel + 11 * x * y + 13 * variant;
            }
        }
    }
    g_assert_true(g_file_set_contents(path, (const char *)data, size, NULL));
    return path;
}

static void cfa_configure(CmosFixture *f, unsigned x, unsigned y)
{
    const uint32_t words[] = {
        0, 4099, 37, x, y, 3087, 23, 43691, 11400,
        NIKON_CMOS_FORMAT_FUNCTIONAL_C170, 2, CFA_STRIDE, 1, 0, RAM_BASE,
    };

    for (unsigned i = 0; i < ARRAY_SIZE(words); i++) {
        if (i * 4 != 0x34) { /* Active buffer index is read-only. */
            write_word(f, i * 4, words[i]);
        }
    }
    write_word(f, 0x48, EXPOSURE);
    write_word(f, 0x58, READOUT);
}

static void cfa_verify(CmosFixture *f, unsigned image_width, unsigned image_height,
                        unsigned crop_x, unsigned crop_y, unsigned variant)
{
    const uint8_t *data = memory_region_get_ram_ptr(&f->ram);

    for (unsigned y = 0; y < CFA_HEIGHT; y++) {
        for (unsigned x = 0; x < CFA_WIDTH; x++) {
            /* Independent scalar reference retains both coordinate floors. */
            uint64_t sensor_x = crop_x + (uint64_t)x * 3087 / CFA_WIDTH;
            uint64_t sensor_y = crop_y + (uint64_t)y * 23 / CFA_HEIGHT;
            unsigned ix = sensor_x * image_width / 4099;
            unsigned iy = sensor_y * image_height / 37;
            unsigned lane = 2 * (y & 1) + (x & 1);
            unsigned channel = lane == 0 ? 0 : lane == 3 ? 2 : 1;
            unsigned response = lane == 0 ? 2198 : lane == 3 ? 2966 : 4096;
            uint8_t source = 37 * ix + 19 * iy + 73 * channel +
                             11 * ix * iy + 13 * variant;
            unsigned expected = (source * 257u * response + 2048) / 4096;
            unsigned actual = data[y * CFA_STRIDE + x * 2] |
                              data[y * CFA_STRIDE + x * 2 + 1] << 8;

            g_assert_cmpuint(actual, ==, expected);
        }
        for (unsigned byte = CFA_WIDTH * 2; byte < CFA_STRIDE; byte++) {
            g_assert_cmphex(data[y * CFA_STRIDE + byte], ==, 0xa5);
        }
    }
    g_assert_cmphex(data[CFA_HEIGHT * CFA_STRIDE], ==, 0xa5);
}

static void test_cfa_coordinate_identity(void)
{
    g_autofree char *image = cfa_image(17, 11, 0);
    g_autofree char *replacement = cfa_image(19, 13, 1);
    uint8_t fine[CFA_HEIGHT * CFA_STRIDE];

    for (unsigned late = 0; late < 2; late++) {
        CmosFixture *f = fixture();
        int64_t start = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

        object_property_set_str(OBJECT(f->dev), "image", image, &error_abort);
        cfa_configure(f, 29, 5);
        write_word(f, 0x60, 1);
        if (late) {
            dispatch_at(start + 100000);
        } else {
            for (unsigned ns = 1; ns <= EXPOSURE + CFA_HEIGHT * 2 * READOUT; ns++) {
                dispatch_at(start + ns);
            }
        }
        g_assert_cmpuint(read_word(f, 0x68), ==, NIKON_CMOS_DONE);
        cfa_verify(f, 17, 11, 29, 5, 0);
        if (!late) {
            memcpy(fine, memory_region_get_ram_ptr(&f->ram), sizeof(fine));
        } else {
            g_assert_cmpmem(memory_region_get_ram_ptr(&f->ram), sizeof(fine),
                            fine, sizeof(fine));
        }
        /* Rebinding after reset changes both image coordinates and crop. */
        device_cold_reset(f->dev);
        object_property_set_str(OBJECT(f->dev), "image", replacement, &error_abort);
        memset(memory_region_get_ram_ptr(&f->ram), 0xa5, RAM_SIZE);
        cfa_configure(f, 1, 9);
        start = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        write_word(f, 0x60, 1);
        dispatch_at(start + 100000);
        g_assert_cmpuint(read_word(f, 0x68), ==, NIKON_CMOS_DONE);
        cfa_verify(f, 19, 13, 1, 9, 1);
        cleanup(f);
    }
    unlink(image);
    unlink(replacement);
}

static void test_static_fullsize_acquire(void)
{
    CmosFixture *f = fixture();
    Error *err = NULL;
    uint64_t token;

    /*
     * 8320 x 5504 full-frame FX static dimensions are within the
     * relaxed bounds.
     */
    token = nikon_cmos_acquire(f->dev, 8320, 5504, &err);
    g_assert_null(err);
    g_assert_cmpuint(token, >, 0);

    /* Dimensions exceeding 8448 or 5632 or 48 MP are rejected. */
    token = nikon_cmos_acquire(f->dev, 8450, 5504, &err);
    g_assert_nonnull(err);
    g_assert_cmpuint(token, ==, 0);
    error_free(err);
    err = NULL;

    token = nikon_cmos_acquire(f->dev, 8320, 5634, &err);
    g_assert_nonnull(err);
    g_assert_cmpuint(token, ==, 0);
    error_free(err);
    err = NULL;

    device_cold_reset(f->dev);
    token = nikon_cmos_acquire(f->dev, 8448, 5632, &err);
    g_assert_null(err);
    g_assert_cmpuint(token, >, 0);

    cleanup(f);
}

int main(int argc, char **argv)
{
    g_autofree char *directory = g_dir_make_tmp("nikon-cmos-XXXXXX", NULL);
    g_autofree char *socket = g_build_filename(directory, "qtest", NULL);
    g_autofree char *chardev = g_strdup_printf("unix:%s,server=on,wait=off", socket);
    const char *const arguments[] = {
        argv[0], "-machine", "none", "-accel", "qtest", "-display", "none",
        "-nodefaults", "-qtest", chardev, NULL
    };
    g_auto(GStrv) qemu_argv = g_new0(char *, G_N_ELEMENTS(arguments));
    int result;

    g_assert_nonnull(directory);
    g_test_init(&argc, &argv, NULL);
    for (unsigned i = 0; i < G_N_ELEMENTS(arguments) - 1; i++) {
        qemu_argv[i] = g_strdup(arguments[i]);
    }
    qemu_init(G_N_ELEMENTS(arguments) - 1, qemu_argv);
    vm_start();
    g_test_add_func("/nikon/cmos/late-dma", test_late_dma);
    g_test_add_func("/nikon/cmos/cancellation", test_cancellation);
    g_test_add_func("/nikon/cmos/reentrant", test_reentrant);
    g_test_add_func("/nikon/cmos/deadline-overflow", test_overflow);
    g_test_add_func("/nikon/cmos/period-anchor", test_period_anchor);
    g_test_add_func("/nikon/cmos/cfa-coordinate-identity",
                    test_cfa_coordinate_identity);
    g_test_add_func("/nikon/cmos/static-fullsize-acquire",
                    test_static_fullsize_acquire);
    result = g_test_run();
    qemu_cleanup(result);
    unlink(socket);
    rmdir(directory);
    return result;
}

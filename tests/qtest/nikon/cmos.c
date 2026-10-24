/* Guest-configured CMOS transport fixture; values are not Nikon mode facts.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "qemu/crc-ccitt.h"
#include "qobject/qdict.h"
#include "libqtest.h"
#include "hw/nikon/cmos.h"
#include "hw/nikon/sensor.h"
#include "hw/nikon/sla.h"

#define CMOS 0x24080000
#define RAM 0xb1810000
#define READOUT_PARAMETERS 0x2006b000
#define PIPELINE_PATH "/machine/readout-pipeline"
#define READOUT_PATH "/machine/readout-control"
#define CAPTURE 0x20068000

static char *image;
static char *source;

static uint64_t property_at(QTestState *q, const char *path,
                            const char *property)
{
    QDict *reply = qtest_qmp(q,
        "{'execute':'qom-get','arguments':{'path':%s,'property':%s}}",
        path, property);
    uint64_t value = qdict_get_int(reply, "return");

    qobject_unref(reply);
    return value;
}

static char *functional_bundle(void)
{
    static const char *const names[] = {
        "ex1840_013000.bi", "_tpj01_v140.bin", "eg1840_mas_01700",
        "vr1840_010100.bi", "eg1840_sla_01700",
    };
    const size_t header = 208;
    const size_t total = header + 3 * 6 + 0x80000 + 2 +
                         NIKON_SLA_ROM_SIZE + 2 + 16;
    g_autofree uint8_t *data = g_malloc0(total);
    char *path = NULL;
    size_t offset = header;
    unsigned i;
    int fd;

    memset(data, ' ', 32);
    stl_be_p(data + 32, ARRAY_SIZE(names));
    stl_be_p(data + 36, header);
    for (i = 0; i < ARRAY_SIZE(names); i++) {
        size_t length = i == 0 ? 0x80000 :
                        i == 4 ? NIKON_SLA_ROM_SIZE : 4;
        uint8_t *entry = data + 48 + i * 32;

        memcpy(entry, names[i], strlen(names[i]));
        stl_be_p(entry + 16, offset);
        stl_be_p(entry + 20, length + 2);
        if (i == 0) {
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
    fd = g_file_open_tmp("nikon-cmos-bundle-XXXXXX", &path, NULL);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    g_assert_true(g_file_set_contents(path, (const char *)data, total, NULL));
    return path;
}

static QTestState *pipeline_start(char **bundle)
{
    g_autofree char *quoted = NULL;
    QTestState *q;

    *bundle = functional_bundle();
    quoted = g_shell_quote(*bundle);
    q = qtest_initf("-M z7ii-mas-diagnostic,bundle=%s "
                       "-smp cpus=2,maxcpus=5 -nodefaults -display none",
                       quoted);
    /* Marker-only fixtures start without a committed first descriptor. */
    qtest_writel(q, CAPTURE + 0x10, 2);
    qtest_writel(q, CAPTURE + 8, 2);
    return q;
}

static void pipeline_configure(QTestState *q, uint32_t destination,
                               uint32_t stride, uint32_t span,
                               uint32_t format)
{
    qtest_writel(q, READOUT_PARAMETERS + 0x3c0, 1);
    qtest_writel(q, READOUT_PARAMETERS + 0x394, 0);
    qtest_writel(q, READOUT_PARAMETERS + 0x3e8, destination);
    qtest_writel(q, READOUT_PARAMETERS + 0x3ec, stride);
    qtest_writel(q, READOUT_PARAMETERS + 0x3f0, span);
    qtest_writel(q, READOUT_PARAMETERS + 0x3f4, destination + 0x10000);
    qtest_writel(q, READOUT_PARAMETERS + 0x3f8, 0x100);
    qtest_writel(q, READOUT_PARAMETERS + 0x3fc, format);
}

static QTestState *start(void)
{
    g_autofree char *bios = g_shell_quote(image);

    QTestState *q = qtest_initf("-M z7ii-mas-diagnostic -smp 1 -bios %s "
                                "-nodefaults -display none", bios);

    /* Explicit fixture source for these transport tests; production machines
     * leave the CMOS provider unbound until firmware evidence supplies one. */
    qtest_qmp_assert_success(q,
        "{'execute':'qom-set','arguments':{'path':'/machine/cmos',"
        "'property':'fixture-provider','value':true}}");

    qtest_irq_intercept_out_named(q, "/machine/cmos", "sysbus-irq");
    return q;
}

static void configure(QTestState *q, unsigned width, unsigned height,
                      unsigned bytes_per_pixel, unsigned stride)
{
    qtest_writel(q, CMOS + 0x004, width);
    qtest_writel(q, CMOS + 0x008, height);
    qtest_writel(q, CMOS + 0x00c, 0);
    qtest_writel(q, CMOS + 0x010, 0);
    qtest_writel(q, CMOS + 0x014, width);
    qtest_writel(q, CMOS + 0x018, height);
    qtest_writel(q, CMOS + 0x01c, 1U << 16);
    qtest_writel(q, CMOS + 0x020, 1U << 16);
    qtest_writel(q, CMOS + 0x024, 0x12345678); /* opaque format word */
    qtest_writel(q, CMOS + 0x094, 0x1badb002); /* transport seed */
    qtest_writel(q, CMOS + 0x028, bytes_per_pixel);
    qtest_writel(q, CMOS + 0x02c, stride);
    qtest_writel(q, CMOS + 0x030, 2);
    qtest_writel(q, CMOS + 0x038, RAM);
    qtest_writel(q, CMOS + 0x03c, RAM + 0x10000);
    qtest_writel(q, CMOS + 0x048, 10);
    qtest_writel(q, CMOS + 0x058, 10);
}

static void test_configured_frames(void)
{
    static const struct {
        unsigned width, height, bytes_per_pixel, stride;
    } modes[] = {
        { 8, 8, 4, 48 }, /* transport regression fixture */
        { 11, 5, 2, 32 },
        { 13, 3, 1, 16 },
    };
    QTestState *q = start();
    uint8_t first[8 * 4], second[8 * 4];
    unsigned i;

    for (i = 0; i < ARRAY_SIZE(modes); i++) {
        unsigned bytes = modes[i].width * modes[i].bytes_per_pixel;
        unsigned active = i & 1;
        uint32_t destination = RAM + active * 0x10000;

        configure(q, modes[i].width, modes[i].height,
                  modes[i].bytes_per_pixel, modes[i].stride);
        qtest_memset(q, destination, 0xa5,
                     modes[i].stride * modes[i].height);
        qtest_writel(q, CMOS + 0x060, 1);
        g_assert_cmphex(qtest_readl(q, CMOS + 0x068), ==, NIKON_CMOS_EXPOSING);
        g_assert_cmphex(qtest_readb(q, destination), ==, 0xa5);
        qtest_clock_step(q, 10000);
        g_assert_cmphex(qtest_readl(q, CMOS + 0x068), ==, NIKON_CMOS_DONE);
        g_assert_cmphex(qtest_readl(q, CMOS + 0x078), ==, i + 1);
        g_assert_cmphex(qtest_readl(q, CMOS + 0x07c), ==, (i + 1) & 1);
        g_assert_cmphex(qtest_readb(q, destination + bytes), ==, 0xa5);
        g_assert_cmphex(qtest_readb(q, destination), !=, 0xa5);
        if (i == 0) {
            qtest_memread(q, destination, first, sizeof(first));
        } else if (i == 1) {
            qtest_memread(q, destination, second, sizeof(second));
        }
    }
    g_assert_cmpint(memcmp(first, second, sizeof(second)), !=, 0);
    g_assert_cmphex(qtest_readl(q, CMOS + 0x070), ==, NIKON_CMOS_EVENT_DONE);
    qtest_writel(q, CMOS + 0x06c, NIKON_CMOS_EVENT_DONE);
    g_assert_true(qtest_get_irq(q, 0));
    qtest_writel(q, CMOS + 0x074, NIKON_CMOS_EVENT_DONE);
    g_assert_false(qtest_get_irq(q, 0));
    g_assert_cmphex(qtest_readl(q, CMOS + 0x070), ==, 0);
    qtest_quit(q);
}

static void test_crop_scale_and_unknown(void)
{
    QTestState *q = start();
    uint8_t cropped[6], full[12];

    configure(q, 6, 4, 1, 16);
    qtest_writel(q, CMOS + 0x100, 0xdeadbeef);
    qtest_writel(q, CMOS + 0x104, 0x0000abcd);
    qtest_writel(q, CMOS + 0x080, 0x13); /* unknown Bayer selector */
    qtest_writel(q, CMOS + 0x084, 0x19); /* unknown bit-depth word */
    qtest_writel(q, CMOS + 0x088, 0x37); /* unknown packing word */
    qtest_writel(q, CMOS + 0x08c, 0xb1817654); /* opaque DMA source */
    qtest_writel(q, CMOS + 0x090, 0xb1814321); /* opaque DMA destination */
    g_assert_cmphex(qtest_readl(q, CMOS + 0x100), ==, 0xdeadbeef);
    g_assert_cmphex(qtest_readl(q, CMOS + 0x104), ==, 0x0000abcd);
    g_assert_cmphex(qtest_readl(q, CMOS + 0x080), ==, 0x13);
    g_assert_cmphex(qtest_readl(q, CMOS + 0x084), ==, 0x19);
    g_assert_cmphex(qtest_readl(q, CMOS + 0x088), ==, 0x37);
    g_assert_cmphex(qtest_readl(q, CMOS + 0x08c), ==, 0xb1817654);
    g_assert_cmphex(qtest_readl(q, CMOS + 0x090), ==, 0xb1814321);
    qtest_memset(q, RAM, 0xa5, 64);
    qtest_writel(q, CMOS + 0x060, 1);
    qtest_clock_step(q, 10000);
    qtest_memread(q, RAM, full, sizeof(full));

    qtest_writel(q, CMOS + 0x00c, 2);
    qtest_writel(q, CMOS + 0x010, 1);
    qtest_writel(q, CMOS + 0x014, 3);
    qtest_writel(q, CMOS + 0x018, 2);
    qtest_writel(q, CMOS + 0x01c, 2U << 16);
    qtest_writel(q, CMOS + 0x020, 1U << 16);
    qtest_writel(q, CMOS + 0x03c, RAM + 0x10000);
    qtest_memset(q, RAM + 0x10000, 0xa5, 64);
    qtest_writel(q, CMOS + 0x060, 1);
    qtest_clock_step(q, 10000);
    qtest_memread(q, RAM + 0x10000, cropped, sizeof(cropped));
    g_assert_cmphex(qtest_readl(q, CMOS + 0x068), ==, NIKON_CMOS_DONE);
    g_assert_cmphex(qtest_readb(q, RAM + 0x10000 + 6), ==, 0xa5);
    g_assert_cmpint(memcmp(full, cropped, sizeof(cropped)), !=, 0);
    qtest_quit(q);
}

static void test_invalid_and_cancel(void)
{
    QTestState *q = start();

    qtest_writel(q, CMOS + 0x060, 1);
    g_assert_cmphex(qtest_readl(q, CMOS + 0x068), ==, NIKON_CMOS_ERROR);
    g_assert_cmphex(qtest_readl(q, CMOS + 0x070), ==, NIKON_CMOS_EVENT_ERROR);
    qtest_writel(q, CMOS + 0x074, NIKON_CMOS_EVENT_ERROR);
    configure(q, 8, 8, 4, 48);
    qtest_memset(q, RAM, 0xa5, 48 * 8);
    qtest_writel(q, CMOS + 0x060, 1);
    qtest_writel(q, CMOS + 0x064, 1);
    qtest_clock_step(q, 10000);
    g_assert_cmphex(qtest_readl(q, CMOS + 0x068), ==, NIKON_CMOS_IDLE);
    g_assert_cmphex(qtest_readl(q, CMOS + 0x070), ==, NIKON_CMOS_EVENT_CANCEL);
    g_assert_cmphex(qtest_readl(q, RAM), ==, 0xa5a5a5a5);
    qtest_qmp_assert_success(q, "{'execute':'system_reset'}");
    qtest_qmp_eventwait(q, "RESET");
    g_assert_cmphex(qtest_readl(q, CMOS + 0x100), ==, 0);
    g_assert_cmphex(qtest_readl(q, CMOS + 0x070), ==, 0);
    qtest_quit(q);
}

static void test_provider_required(void)
{
    QTestState *q = start();
    QDict *reply;

    qtest_qmp_assert_success(q,
        "{'execute':'qom-set','arguments':{'path':'/machine/cmos',"
        "'property':'fixture-provider','value':false}}");
    reply = qtest_qmp(q,
        "{'execute':'qom-get','arguments':{'path':'/machine/cmos',"
        "'property':'provider-bound'}}");
    g_assert_false(qdict_get_bool(reply, "return"));
    qobject_unref(reply);

    /* Geometry alone cannot make an unbound CMOS source produce a frame. */
    configure(q, 8, 8, 4, 48);
    qtest_writel(q, CMOS + 0x060, 1);
    g_assert_cmphex(qtest_readl(q, CMOS + 0x068), ==, NIKON_CMOS_ERROR);
    g_assert_cmphex(qtest_readl(q, CMOS + 0x070), ==,
                    NIKON_CMOS_EVENT_ERROR);
    qtest_quit(q);
}

static void test_image_provider(void)
{
    QTestState *q = start();
    uint8_t output[4];
    qtest_qmp_assert_success(q,
        "{'execute':'qom-set','arguments':{'path':'/machine/cmos',"
        "'property':'image','value':%s}}", source);
    configure(q, 2, 2, 1, 2);
    qtest_memset(q, RAM, 0xa5, 4);
    qtest_writel(q, CMOS + 0x060, 1);
    qtest_clock_step(q, 10000);
    qtest_memread(q, RAM, output, sizeof(output));
    g_assert_cmphex(output[0], ==, 0);
    g_assert_cmphex(output[1], ==, 64);
    g_assert_cmphex(output[2], ==, 128);
    g_assert_cmphex(output[3], ==, 255);
    g_assert_cmphex(qtest_readl(q, CMOS + 0x068), ==, NIKON_CMOS_DONE);
    qtest_quit(q);
}

static void test_period(void)
{
    QTestState *q = start();

    configure(q, 4, 2, 1, 8);
    qtest_writel(q, CMOS + 0x050, 100000);
    qtest_writel(q, CMOS + 0x060, 1);
    qtest_clock_step(q, 10000);
    g_assert_cmphex(qtest_readl(q, CMOS + 0x068), ==, NIKON_CMOS_DONE);
    qtest_writel(q, CMOS + 0x060, 1);
    g_assert_cmphex(qtest_readl(q, CMOS + 0x068), ==, NIKON_CMOS_ERROR);
    g_assert_cmphex(qtest_readl(q, CMOS + 0x078), ==, 1);
    qtest_clock_step(q, 100001);
    qtest_writel(q, CMOS + 0x060, 1);
    qtest_clock_step(q, 10000);
    g_assert_cmphex(qtest_readl(q, CMOS + 0x068), ==, NIKON_CMOS_DONE);
    g_assert_cmphex(qtest_readl(q, CMOS + 0x078), ==, 2);
    qtest_quit(q);
}

static void test_pipeline_valid_and_silent(void)
{
    g_autofree char *bundle = NULL;
    QTestState *q = pipeline_start(&bundle);

    qtest_memset(q, RAM, 0xa5, 0x200);
    qtest_memset(q, RAM + 0x10000, 0x5a, 0x200);
    pipeline_configure(q, RAM, 0x100, 0x200,
                       NIKON_READOUT_C170_FORMAT_PRIMARY);
    qtest_clock_step(q, 10000);
    g_assert_cmphex(qtest_readl(q, RAM), ==, 0xa5a5a5a5);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "requests"), ==, 0);

    qtest_writel(q, READOUT_PARAMETERS + 0x400, 1);
    g_assert_cmphex(qtest_readl(q, READOUT_PARAMETERS + 0x3c0), ==, 1);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "accepted"), ==, 1);
    qtest_clock_step(q, 10000);
    g_assert_cmphex(qtest_readl(q, RAM), !=, 0xa5a5a5a5);
    g_assert_cmphex(qtest_readl(q, RAM + 0x10000), ==, 0x5a5a5a5a);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "completed"), ==, 1);
    g_assert_cmpuint(property_at(q, READOUT_PATH, "completion-generation"),
                     ==, 1);
    qtest_quit(q);
    g_assert_cmpint(unlink(bundle), ==, 0);
}

static void test_pipeline_run_completion(void)
{
    g_autofree char *bundle = NULL;
    QTestState *q = pipeline_start(&bundle);

    qtest_writel(q, CAPTURE + 0xc, 2);
    qtest_memset(q, RAM, 0xa5, 0x200);
    pipeline_configure(q, RAM, 0x100, 0x200,
                       NIKON_READOUT_C170_FORMAT_ALTERNATE);
    qtest_writel(q, READOUT_PARAMETERS + 0x3c0, 1);
    qtest_writel(q, READOUT_PARAMETERS + 0x394, 0);
    qtest_writel(q, CAPTURE + 0x200, 8);
    qtest_writel(q, CAPTURE + 0x100, 2);
    qtest_writel(q, CAPTURE + 0x104, 2);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "requests"), ==, 0);
    qtest_writel(q, READOUT_PARAMETERS + 0x3e8, RAM + 0x20000);
    qtest_writel(q, READOUT_PARAMETERS + 0x394, 1U << 16);
    qtest_writel(q, CAPTURE + 0x10, 2);
    qtest_writel(q, CAPTURE + 8, 2);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "accepted"), ==, 1);
    g_assert_cmphex(qtest_readl(q, READOUT_PARAMETERS + 0x400), ==, 0);
    qtest_writel(q, CAPTURE + 8, 2);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "requests"), ==, 1);
    g_assert_cmphex(qtest_readl(q, CAPTURE + 0x210), ==, 0);
    g_assert_cmphex(qtest_readl(q, RAM), ==, 0xa5a5a5a5);
    /* Staging changes cannot change the in-flight destination or IRQ mode. */
    pipeline_configure(q, RAM + 0x20000, 0x100, 0x200,
                       NIKON_READOUT_C170_FORMAT_PRIMARY);
    qtest_writel(q, READOUT_PARAMETERS + 0x394, 1U << 16);
    qtest_clock_step(q, 10000);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "completed"), ==, 1);
    g_assert_cmphex(qtest_readl(q, RAM), !=, 0xa5a5a5a5);
    g_assert_cmphex(qtest_readl(q, CAPTURE + 0x210), ==, 8);
    qtest_writel(q, CAPTURE + 0x210, 8);
    qtest_writel(q, READOUT_PARAMETERS + 0x400, 1);
    qtest_clock_step(q, 10000);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "completed"), ==, 2);
    g_assert_cmphex(qtest_readl(q, CAPTURE + 0x210), ==, 0x20);
    qtest_quit(q);
    g_assert_cmpint(unlink(bundle), ==, 0);
}

static void test_pipeline_unit_cancellation(void)
{
    static const unsigned cancel[] = { 0xc, 0, 0x14 };

    for (unsigned i = 0; i < ARRAY_SIZE(cancel); i++) {
        g_autofree char *bundle = NULL;
        QTestState *q = pipeline_start(&bundle);

        pipeline_configure(q, RAM, 0x100, 0x200,
                           NIKON_READOUT_C170_FORMAT_PRIMARY);
        qtest_memset(q, RAM, 0xa5, 0x200);
        qtest_writel(q, READOUT_PARAMETERS + 0x400, 1);
        g_assert_cmpuint(property_at(q, PIPELINE_PATH, "accepted"), ==, 1);
        qtest_writel(q, CAPTURE + cancel[i], 2);
        qtest_clock_step(q, 10000);
        g_assert_cmphex(qtest_readl(q, RAM), ==, 0xa5a5a5a5);
        g_assert_cmphex(qtest_readl(q, CAPTURE + 0x210), ==, 0);
        g_assert_cmpuint(property_at(q, PIPELINE_PATH, "cancelled"), ==, 1);
        g_assert_cmpuint(property_at(q, PIPELINE_PATH, "completed"), ==, 0);
        g_assert_cmpuint(property_at(q, READOUT_PATH,
                                     "completion-generation"), ==, 0);
        qtest_quit(q);
        g_assert_cmpint(unlink(bundle), ==, 0);
    }
}

static void test_pipeline_rejections(void)
{
    g_autofree char *bundle = NULL;
    QTestState *q = pipeline_start(&bundle);

    qtest_memset(q, RAM, 0xa5, 0x400);
    pipeline_configure(q, RAM, 0x100, 0x200, 0x00020002);
    qtest_writel(q, READOUT_PARAMETERS + 0x400, 1);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "rejected"), ==, 1);
    g_assert_cmphex(qtest_readl(q, RAM), ==, 0xa5a5a5a5);

    pipeline_configure(q, RAM, 0x80, 0x200,
                       NIKON_READOUT_C170_FORMAT_ALTERNATE);
    qtest_writel(q, READOUT_PARAMETERS + 0x400, 1);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "rejected"), ==, 2);

    pipeline_configure(q, 0xfffffff0, 0x100, 0x200,
                       NIKON_READOUT_C170_FORMAT_PRIMARY);
    qtest_writel(q, READOUT_PARAMETERS + 0x400, 1);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "rejected"), ==, 3);

    pipeline_configure(q, 0x10020000, 0x100, 0x200,
                       NIKON_READOUT_C170_FORMAT_PRIMARY);
    qtest_writel(q, READOUT_PARAMETERS + 0x400, 1);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "rejected"), ==, 4);
    pipeline_configure(q, RAM, 0x100, 0x200,
                       NIKON_READOUT_C170_FORMAT_PRIMARY);
    qtest_writel(q, READOUT_PARAMETERS + 0x3c0, 0);
    qtest_writel(q, READOUT_PARAMETERS + 0x400, 1);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "rejected"), ==, 5);
    qtest_writel(q, READOUT_PARAMETERS + 0x3c0, 1);
    qtest_writel(q, READOUT_PARAMETERS + 0x394, 2U << 16);
    qtest_writel(q, READOUT_PARAMETERS + 0x400, 1);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "rejected"), ==, 6);
    qtest_clock_step(q, 10000);
    g_assert_cmphex(qtest_readl(q, CAPTURE + 0x210), ==, 0);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "active-generation"), ==, 0);
    g_assert_cmpuint(property_at(q, READOUT_PATH, "completion-generation"),
                     ==, 0);
    qtest_quit(q);
    g_assert_cmpint(unlink(bundle), ==, 0);
}

static void test_pipeline_backpressure_and_stale(void)
{
    g_autofree char *bundle = NULL;
    QTestState *q = pipeline_start(&bundle);

    qtest_memset(q, RAM, 0xa5, 0x200);
    pipeline_configure(q, RAM, 0x100, 0x200,
                       NIKON_READOUT_C170_FORMAT_PRIMARY);
    qtest_writel(q, READOUT_PARAMETERS + 0x400, 1);
    qtest_writel(q, READOUT_PARAMETERS + 0x400, 1);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "accepted"), ==, 1);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "rejected"), ==, 1);
    qtest_clock_step(q, 10000);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "stale"), ==, 0);
    g_assert_cmpuint(property_at(q, READOUT_PATH, "completion-generation"),
                     ==, 1);

    qtest_writel(q, READOUT_PARAMETERS + 0x400, 1);
    qtest_clock_step(q, 10000);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "completed"), ==, 2);
    g_assert_cmpuint(property_at(q, READOUT_PATH, "completion-generation"),
                     ==, 2);
    qtest_quit(q);
    g_assert_cmpint(unlink(bundle), ==, 0);
}

static void test_pipeline_cancel_and_reset(void)
{
    g_autofree char *bundle = NULL;
    QTestState *q = pipeline_start(&bundle);

    qtest_memset(q, RAM, 0xa5, 0x200);
    pipeline_configure(q, RAM, 0x100, 0x200,
                       NIKON_READOUT_C170_FORMAT_PRIMARY);
    qtest_writel(q, READOUT_PARAMETERS + 0x400, 1);
    qtest_writel(q, CMOS + 0x064, 1);
    qtest_clock_step(q, 10000);
    g_assert_cmphex(qtest_readl(q, RAM), ==, 0xa5a5a5a5);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "cancelled"), ==, 1);
    g_assert_cmpuint(property_at(q, READOUT_PATH, "completion-generation"),
                     ==, 0);

    qtest_writel(q, READOUT_PARAMETERS + 0x400, 1);
    qtest_qmp_assert_success(q, "{'execute':'system_reset'}");
    qtest_qmp_eventwait(q, "RESET");
    qtest_clock_step(q, 10000);
    g_assert_cmphex(qtest_readl(q, RAM), ==, 0xa5a5a5a5);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "active-generation"), ==, 0);
    g_assert_cmpuint(property_at(q, READOUT_PATH, "request-generation"), ==, 0);
    g_assert_cmpuint(property_at(q, READOUT_PATH, "completion-generation"),
                     ==, 0);
    qtest_quit(q);
    g_assert_cmpint(unlink(bundle), ==, 0);
}

int main(int argc, char **argv)
{
    uint32_t idle = cpu_to_le32(0xeafffffe);
    static const uint8_t pnm[] = {
        'P', '5', '\n', '2', ' ', '2', '\n', '2', '5', '5', '\n',
        0, 64, 128, 255,
    };
    int fd, result;

    g_test_init(&argc, &argv, NULL);
    fd = g_file_open_tmp("nikoncmosbiosXXXXXX", &image, NULL);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    g_assert_true(g_file_set_contents(image, (char *)&idle,
                                      sizeof(idle), NULL));
    fd = g_file_open_tmp("nikoncmosimageXXXXXX", &source, NULL);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    g_assert_true(g_file_set_contents(source, (const char *)pnm,
                                      sizeof(pnm), NULL));
    qtest_add_func("/nikon/cmos/configured-frames", test_configured_frames);
    qtest_add_func("/nikon/cmos/crop-scale-unknown", test_crop_scale_and_unknown);
    qtest_add_func("/nikon/cmos/invalid-cancel", test_invalid_and_cancel);
    qtest_add_func("/nikon/cmos/provider-required", test_provider_required);
    qtest_add_func("/nikon/cmos/image-provider", test_image_provider);
    qtest_add_func("/nikon/cmos/period", test_period);
    qtest_add_func("/nikon/cmos/pipeline/valid-silent",
                   test_pipeline_valid_and_silent);
    qtest_add_func("/nikon/cmos/pipeline/run-completion",
                   test_pipeline_run_completion);
    qtest_add_func("/nikon/cmos/pipeline/unit-cancellation",
                   test_pipeline_unit_cancellation);
    qtest_add_func("/nikon/cmos/pipeline/rejections",
                   test_pipeline_rejections);
    qtest_add_func("/nikon/cmos/pipeline/backpressure-stale",
                   test_pipeline_backpressure_and_stale);
    qtest_add_func("/nikon/cmos/pipeline/cancel-reset",
                   test_pipeline_cancel_and_reset);
    result = g_test_run();
    unlink(image);
    g_free(image);
    unlink(source);
    g_free(source);
    return result;
}

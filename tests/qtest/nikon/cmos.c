/* Guest-configured CMOS transport fixture; values are not Nikon mode facts.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "qemu/crc-ccitt.h"
#include "qobject/qdict.h"
#include "qobject/qlist.h"
#include "libqtest.h"
#include "hw/nikon/cmos.h"
#include "hw/nikon/sensor.h"
#include "hw/nikon/sla.h"
#include "fixture.h"

#define CMOS 0x24080000
#define RAM 0xb1810000
#define READOUT_PARAMETERS 0x2006b000
#define PIPELINE_PATH "/machine/readout-pipeline"
#define READOUT_PATH "/machine/readout-control"
#define CAPTURE 0x20068000

static char *image;
static char *source;

static void statistics_configure(QTestState *q);
static void statistics_rise(QTestState *q);

static uint64_t property_at(QTestState *q, const char *path,
                            const char *property)
{
    QDict *reply = qtest_qmp(q,
        "{'execute':'qom-get','arguments':{'path':%s,'property':%s}}",
        path, property);
    uint64_t value;

    g_assert_true(qdict_haskey(reply, "return"));
    value = qdict_get_int(reply, "return");

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

static QTestState *pipeline_start(char **bundle, const char *extra_options)
{
    g_autofree char *quoted = NULL;
    QTestState *q;

    *bundle = functional_bundle();
    quoted = g_shell_quote(*bundle);
    q = qtest_initf("-M z7ii-mas-diagnostic%s,bundle=%s "
                       "-smp cpus=2,maxcpus=5 -nodefaults -display none",
                       extra_options ? extra_options : "", quoted);
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

static void pipeline_input(QTestState *q)
{
    qtest_set_irq_in(q, PIPELINE_PATH, "automatic-rise", 0, 0);
    qtest_set_irq_in(q, PIPELINE_PATH, "automatic-rise", 0, 1);
    qtest_set_irq_in(q, PIPELINE_PATH, "automatic-rise", 0, 0);
}

static void pipeline_request(QTestState *q)
{
    qtest_writel(q, READOUT_PARAMETERS + 0x400, 1);
    pipeline_input(q);
}

static void test_pipeline_input_boundary(void)
{
    g_autofree char *bundle = NULL;
    QTestState *q = pipeline_start(&bundle, NULL);

    pipeline_configure(q, RAM, 0x100, 0x200,
                       NIKON_READOUT_C170_FORMAT_PRIMARY);
    qtest_memset(q, RAM, 0xa5, 0x200);
    qtest_memset(q, RAM + 0x20000, 0x5a, 0x200);
    qtest_writel(q, READOUT_PARAMETERS + 0x400, 1);
    /* A completion without an admitted transfer cannot satisfy this request. */
    qtest_set_irq_in(q, PIPELINE_PATH, "cmos-event",
                     NIKON_CMOS_PIPELINE_DONE, 1);
    qtest_set_irq_in(q, PIPELINE_PATH, "cmos-event",
                     NIKON_CMOS_PIPELINE_DONE, 0);
    qtest_clock_step(q, 10000);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "completed"), ==, 0);
    g_assert_cmphex(qtest_readl(q, RAM), ==, 0xa5a5a5a5);
    g_assert_cmpuint(property_at(q, READOUT_PATH, "ready-generation"), ==, 1);

    /* A queued generation owns its descriptor before the input arrives. */
    pipeline_configure(q, RAM + 0x20000, 0x100, 0x200,
                       NIKON_READOUT_C170_FORMAT_PRIMARY);
    qtest_writel(q, READOUT_PARAMETERS + 0x394, 1U << 16);
    qtest_writel(q, READOUT_PARAMETERS + 0x400, 1);
    g_assert_cmpuint(property_at(q, READOUT_PATH, "request-generation"), ==, 1);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "rejected"), ==, 1);
    pipeline_input(q);
    pipeline_input(q); /* An extra boundary while DMA is busy is discarded. */
    qtest_clock_step(q, 10000);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "completed"), ==, 1);
    g_assert_cmphex(qtest_readl(q, RAM), !=, 0xa5a5a5a5);
    g_assert_cmphex(qtest_readl(q, RAM + 0x20000), ==, 0x5a5a5a5a);
    g_assert_cmphex(qtest_readl(q, CAPTURE + 0x210), ==, 8);

    /* Neither a consumed edge nor a repeated high level admits a new job. */
    qtest_set_irq_in(q, PIPELINE_PATH, "automatic-rise", 0, 1);
    qtest_writel(q, READOUT_PARAMETERS + 0x400, 1);
    qtest_set_irq_in(q, PIPELINE_PATH, "automatic-rise", 0, 1);
    qtest_clock_step(q, 10000);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "completed"), ==, 1);
    qtest_writel(q, CAPTURE + 0xc, 2);
    pipeline_input(q);
    qtest_clock_step(q, 10000);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "cancelled"), ==, 1);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "active-generation"), ==, 0);
    g_assert_cmphex(qtest_readl(q, RAM + 0x20000), ==, 0x5a5a5a5a);

    qtest_writel(q, CAPTURE + 8, 2);
    qtest_writel(q, READOUT_PARAMETERS + 0x400, 1);
    qtest_system_reset(q);
    pipeline_input(q);
    qtest_clock_step(q, 10000);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "active-generation"), ==, 0);
    g_assert_cmpuint(property_at(q, READOUT_PATH, "ready-generation"), ==, 0);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "completed"), ==, 1);

    /* Board wiring: forced levels do not acquire input; automatic TG does. */
    qtest_writel(q, CAPTURE + 0x10, 2);
    qtest_writel(q, CAPTURE + 8, 2);
    pipeline_configure(q, RAM + 0x20000, 0x100, 0x200,
                       NIKON_READOUT_C170_FORMAT_PRIMARY);
    qtest_writel(q, READOUT_PARAMETERS + 0x400, 1);
    qtest_writel(q, 0x20060070, 3);
    qtest_writel(q, 0x20060010, 1);
    qtest_clock_step(q, 100000000);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "completed"), ==, 1);
    qtest_writel(q, 0x20060070, 0);
    qtest_clock_step(q, 16666666);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "completed"), ==, 1);
    qtest_clock_step(q, 10000);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "completed"), ==, 2);
    g_assert_cmphex(qtest_readl(q, RAM + 0x20000), !=, 0x5a5a5a5a);
    qtest_quit(q);
    g_assert_cmpint(unlink(bundle), ==, 0);
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

static void test_image_source_contract(void)
{
    static const uint8_t pnm[] = {
        'P', '5', '\n', '2', ' ', '2', '\n', '2', '5', '5', '\n',
        '\n', '\r', ' ', '#',
    };
    g_autofree char *path = NULL;
    QTestState *q = start();
    QDict *reply;
    uint8_t output[4];
    uint8_t cropped[8];
    static const uint8_t expected_crop[] = { 10, 13, 0xa5, 0xa5,
                                            32, 35, 0xa5, 0xa5 };
    int fd = g_file_open_tmp("nikon-cmos-source-XXXXXX", &path, NULL);

    g_assert_cmpint(fd, >=, 0);
    close(fd);
    g_assert_true(g_file_set_contents(path, (const char *)pnm,
                                     sizeof(pnm), NULL));
    qtest_qmp_assert_success(q,
        "{'execute':'qom-set','arguments':{'path':'/machine/cmos',"
        "'property':'fixture-provider','value':false}}");
    qtest_qmp_assert_success(q,
        "{'execute':'qom-set','arguments':{'path':'/machine/cmos',"
        "'property':'image','value':%s}}", path);
    reply = qtest_qmp(q,
        "{'execute':'qom-get','arguments':{'path':'/machine/cmos',"
        "'property':'provider-bound'}}");
    g_assert_true(qdict_get_bool(reply, "return"));
    qobject_unref(reply);
    configure(q, 2, 2, 1, 2);
    qtest_writel(q, CMOS + 0x060, 1);
    qtest_clock_step(q, 10000);
    g_assert_cmpuint(qtest_readl(q, CMOS + 0x068), ==, NIKON_CMOS_DONE);
    qtest_memread(q, RAM, output, sizeof(output));
    g_assert_cmpmem(output, sizeof(output), pnm + sizeof(pnm) - 4, 4);
    g_assert_true(g_file_set_contents(path, "invalid", 7, NULL));
    /* Sensor coordinates differ from both scene and output coordinates. */
    configure(q, 4, 4, 1, 4);
    qtest_writel(q, CMOS + 0x00c, 1);
    qtest_writel(q, CMOS + 0x010, 1);
    qtest_writel(q, CMOS + 0x014, 2);
    qtest_writel(q, CMOS + 0x018, 2);
    qtest_memset(q, RAM + 0x10000, 0xa5, sizeof(cropped));
    /* Changing source during exposure cannot replace this frame's bytes. */
    qtest_writel(q, CMOS + 0x060, 1);
    reply = qtest_qmp(q,
        "{'execute':'qom-set','arguments':{'path':'/machine/cmos',"
        "'property':'image','value':''}}");
    g_assert_true(qdict_haskey(reply, "error"));
    qobject_unref(reply);
    qtest_clock_step(q, 10000);
    qtest_memread(q, RAM + 0x10000, cropped, sizeof(cropped));
    g_assert_cmpmem(cropped, sizeof(cropped), expected_crop,
                    sizeof(expected_crop));
    /* Explicit reassignment reloads; malformed input cannot use the old image. */
    qtest_qmp_assert_success(q,
        "{'execute':'qom-set','arguments':{'path':'/machine/cmos',"
        "'property':'image','value':%s}}", path);
    qtest_memset(q, RAM, 0xa5, 4);
    qtest_writel(q, CMOS + 0x060, 1);
    g_assert_cmpuint(qtest_readl(q, CMOS + 0x068), ==, NIKON_CMOS_ERROR);
    g_assert_cmphex(qtest_readl(q, RAM), ==, 0xa5a5a5a5);
    qtest_qmp_assert_success(q,
        "{'execute':'qom-set','arguments':{'path':'/machine/cmos',"
        "'property':'image','value':''}}");
    reply = qtest_qmp(q,
        "{'execute':'qom-get','arguments':{'path':'/machine/cmos',"
        "'property':'provider-bound'}}");
    g_assert_false(qdict_get_bool(reply, "return"));
    qobject_unref(reply);
    qtest_writel(q, CMOS + 0x060, 1);
    g_assert_cmpuint(qtest_readl(q, CMOS + 0x068), ==, NIKON_CMOS_ERROR);
    qtest_quit(q);
    unlink(path);
}

static void test_image_gray_transport(void)
{
    static const char header[] = "P6\r\n2 2\r\n255\r\n";
    static const uint8_t colors[][3] = {
        { 255, 0, 0 }, { 0, 255, 0 }, { 0, 0, 255 }, { 255, 255, 255 },
    };
    static const uint8_t expected[] = { 77, 149, 29, 255 };
    g_autofree char *bundle = NULL;
    g_autofree char *path = NULL;
    QTestState *q = pipeline_start(&bundle, NULL);
    uint8_t ppm[sizeof(header) - 1 + 4 * 3];
    uint8_t output[512];
    int fd = g_file_open_tmp("nikon-cmos-gray-XXXXXX", &path, NULL);

    g_assert_cmpint(fd, >=, 0);
    close(fd);
    memcpy(ppm, header, sizeof(header) - 1);
    for (unsigned i = 0; i < 4; i++) {
        memcpy(ppm + sizeof(header) - 1 + 3 * i, colors[i], 3);
    }
    g_assert_true(g_file_set_contents(path, (const char *)ppm,
                                     sizeof(ppm), NULL));
    qtest_qmp_assert_success(q,
        "{'execute':'qom-set','arguments':{'path':'/machine/cmos',"
        "'property':'image','value':%s}}", path);
    qtest_qmp_assert_success(q,
        "{'execute':'qom-set','arguments':{'path':'/machine/cmos',"
        "'property':'fixture-provider','value':false}}");
    statistics_configure(q);
    pipeline_configure(q, RAM, 256, 512, NIKON_READOUT_C170_FORMAT_PRIMARY);
    pipeline_request(q);
    qtest_clock_step(q, 10000);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "completed"), ==, 1);
    qtest_memread(q, RAM, output, sizeof(output));
    for (unsigned i = 0; i < 256; i++) {
        g_assert_cmphex(lduw_le_p(output + 2 * i), ==,
                        expected[i / 64] * 257u);
    }
    statistics_rise(q);
    qtest_clock_step(q, 2000000);
    g_assert_cmpuint(property_at(q, "/machine/capture-statistics",
                                 "completed"), ==, 1);
    for (unsigned i = 0; i < 4; i++) {
        g_assert_cmphex(qtest_readl(q, 0x2006a3e0 + i * 4), ==,
                        expected[i] * 257u);
    }
    g_assert_cmphex(qtest_readl(q, RAM + 0x111000), ==, 0x4d4d4d4d);
    g_assert_cmphex(qtest_readl(q, CAPTURE + 0x270), ==, 0x20);
    configure(q, 128, 2, 1, 128);
    qtest_writel(q, CMOS + 0x024, NIKON_CMOS_FORMAT_FUNCTIONAL_C170);
    qtest_writel(q, CMOS + 0x060, 1);
    g_assert_cmpuint(qtest_readl(q, CMOS + 0x068), ==, NIKON_CMOS_ERROR);
    qtest_quit(q);
    unlink(bundle);
    unlink(path);
}

static void test_image_raster_boundaries(void)
{
    static const char *const data[] = {
        "P5\n# header\n2 2\n255\n\n\r #",
        "P5\r\n2 2\r\n255\r\n\n\r #",
        "P5\n2 2\n255#\n\r #",
        "P5\n2 2\n255\n\n\r ",
        "P5\n2 2\n65535\n\n\r #",
        "P7\n2 2\n255\n\n\r #",
        "P5\n2 2\n255\n\n\r #",
    };
    static const uint8_t pixels[] = { 10, 13, 32, 35 };

    for (unsigned i = 0; i < ARRAY_SIZE(data); i++) {
        g_autofree char *path = NULL;
        QTestState *q = start();
        uint8_t output[4];
        int fd = g_file_open_tmp("nikon-cmos-raster-XXXXXX", &path, NULL);

        g_assert_cmpint(fd, >=, 0);
        close(fd);
        g_assert_true(g_file_set_contents(path, data[i], strlen(data[i]), NULL));
        if (i == ARRAY_SIZE(data) - 1) {
            unlink(path);
        }
        qtest_qmp_assert_success(q,
            "{'execute':'qom-set','arguments':{'path':'/machine/cmos',"
            "'property':'image','value':%s}}", path);
        configure(q, 2, 2, 1, 2);
        qtest_memset(q, RAM, 0xa5, sizeof(output));
        qtest_writel(q, CMOS + 0x060, 1);
        qtest_clock_step(q, 10000);
        qtest_memread(q, RAM, output, sizeof(output));
        if (i < 2) {
            g_assert_cmpuint(qtest_readl(q, CMOS + 0x068), ==, NIKON_CMOS_DONE);
            g_assert_cmpmem(output, sizeof(output), pixels, sizeof(pixels));
        } else {
            g_assert_cmpuint(qtest_readl(q, CMOS + 0x068), ==, NIKON_CMOS_ERROR);
            g_assert_cmphex((uint32_t)ldl_le_p(output), ==, 0xa5a5a5a5);
        }
        qtest_quit(q);
        unlink(path);
    }
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
    QTestState *q = pipeline_start(&bundle, NULL);

    qtest_memset(q, RAM, 0xa5, 0x200);
    qtest_memset(q, RAM + 0x10000, 0x5a, 0x200);
    pipeline_configure(q, RAM, 0x100, 0x200,
                       NIKON_READOUT_C170_FORMAT_PRIMARY);
    qtest_clock_step(q, 10000);
    g_assert_cmphex(qtest_readl(q, RAM), ==, 0xa5a5a5a5);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "requests"), ==, 0);

    pipeline_request(q);
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
    QTestState *q = pipeline_start(&bundle, NULL);

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
    pipeline_input(q);
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
    pipeline_request(q);
    qtest_clock_step(q, 10000);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "completed"), ==, 2);
    g_assert_cmphex(qtest_readl(q, CAPTURE + 0x210), ==, 0x20);
    qtest_quit(q);
    g_assert_cmpint(unlink(bundle), ==, 0);
}

static void test_pipeline_input_sync(void)
{
    g_autofree char *bundle = NULL;
    QTestState *q = pipeline_start(&bundle, NULL);

    qtest_irq_intercept_out_named(q, "/machine/capture-control", "sysbus-irq");
    pipeline_configure(q, RAM, 0x100, 0x200,
                       NIKON_READOUT_C170_FORMAT_PRIMARY);
    qtest_writel(q, READOUT_PARAMETERS + 0x394, 1U << 16);
    qtest_writel(q, CAPTURE + 0x10, 1);
    qtest_writel(q, CAPTURE + 8, 1);
    qtest_memset(q, RAM, 0xa5, 0x200);
    qtest_writel(q, READOUT_PARAMETERS + 0x400, 1);
    qtest_clock_step(q, 10000);
    /* A run request, elapsed time or unrelated DONE is not acquired input. */
    qtest_set_irq_in(q, PIPELINE_PATH, "cmos-event",
                     NIKON_CMOS_PIPELINE_DONE, 1);
    qtest_set_irq_in(q, PIPELINE_PATH, "cmos-event",
                     NIKON_CMOS_PIPELINE_DONE, 0);
    g_assert_cmphex(qtest_readl(q, CAPTURE + 0x210), ==, 0);
    pipeline_input(q);
    qtest_set_irq_in(q, PIPELINE_PATH, "cmos-event",
                     NIKON_CMOS_PIPELINE_DONE, 1);
    qtest_set_irq_in(q, PIPELINE_PATH, "cmos-event",
                     NIKON_CMOS_PIPELINE_DONE, 0);
    /* Even during acquisition, the provider token must actually be DONE. */
    g_assert_cmphex(qtest_readl(q, CAPTURE + 0x210), ==, 0);
    qtest_clock_step(q, 10000);
    g_assert_cmphex(qtest_readl(q, RAM), !=, 0xa5a5a5a5);
    /* Input sync and auxiliary completion; no frontend-processing ACK. */
    g_assert_cmphex(qtest_readl(q, CAPTURE + 0x210), ==, 0x21);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "input-notifications"), ==, 1);
    g_assert_false(qtest_get_irq(q, 0));
    qtest_writel(q, CAPTURE + 0x200, 1);
    g_assert_true(qtest_get_irq(q, 0));
    qtest_writel(q, CAPTURE + 0x210, 1);
    g_assert_false(qtest_get_irq(q, 0));
    g_assert_cmphex(qtest_readl(q, CAPTURE + 0x210), ==, 0x20);
    qtest_writel(q, CAPTURE + 0x210, 0x20);

    /* Stopping and restarting unit1 cannot qualify an older transfer. */
    pipeline_request(q);
    qtest_writel(q, CAPTURE + 0xc, 1);
    qtest_writel(q, CAPTURE + 8, 1);
    qtest_clock_step(q, 100);
    g_assert_cmphex(qtest_readl(q, CAPTURE + 0x210), ==, 0x20);
    qtest_writel(q, CAPTURE + 0x210, 0x20);

    /* A late start cannot turn previously acquired input into a sync. */
    qtest_writel(q, CAPTURE + 0xc, 1);
    pipeline_request(q);
    qtest_writel(q, CAPTURE + 8, 1);
    qtest_clock_step(q, 100);
    g_assert_cmphex(qtest_readl(q, CAPTURE + 0x210), ==, 0x20);
    qtest_writel(q, CAPTURE + 0x210, 0x20);

    pipeline_request(q);
    qtest_clock_step(q, 100);
    g_assert_cmphex(qtest_readl(q, CAPTURE + 0x210), ==, 0x21);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "input-notifications"), ==, 2);
    qtest_writel(q, CAPTURE + 0x210, 0x21);

    pipeline_request(q);
    qtest_writel(q, CAPTURE + 0xc, 2);
    qtest_clock_step(q, 100);
    g_assert_cmphex(qtest_readl(q, CAPTURE + 0x210), ==, 0);
    qtest_writel(q, CAPTURE + 8, 2);
    pipeline_request(q);
    /* Reset a separately qualified active transfer, without prior cancel. */
    qtest_system_reset(q);
    pipeline_input(q);
    qtest_clock_step(q, 10000);
    g_assert_cmphex(qtest_readl(q, CAPTURE + 0x210), ==, 0);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "input-notifications"), ==, 2);

    qtest_qmp_assert_success(q,
        "{'execute':'qom-set','arguments':{'path':'/machine/cmos',"
        "'property':'fixture-provider','value':false}}");
    pipeline_configure(q, RAM, 0x100, 0x200,
                       NIKON_READOUT_C170_FORMAT_PRIMARY);
    qtest_writel(q, READOUT_PARAMETERS + 0x394, 1U << 16);
    qtest_writel(q, CAPTURE + 0x10, 3);
    qtest_writel(q, CAPTURE + 8, 3);
    pipeline_request(q);
    qtest_clock_step(q, 100);
    g_assert_cmphex(qtest_readl(q, CAPTURE + 0x210), ==, 0);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "input-notifications"), ==, 2);
    qtest_quit(q);
    g_assert_cmpint(unlink(bundle), ==, 0);
}

static void test_pipeline_unit_cancellation(void)
{
    static const unsigned cancel[] = { 0xc, 0, 0x14 };

    for (unsigned i = 0; i < ARRAY_SIZE(cancel); i++) {
        g_autofree char *bundle = NULL;
        QTestState *q = pipeline_start(&bundle, NULL);

        pipeline_configure(q, RAM, 0x100, 0x200,
                           NIKON_READOUT_C170_FORMAT_PRIMARY);
        qtest_memset(q, RAM, 0xa5, 0x200);
        pipeline_request(q);
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
    QTestState *q = pipeline_start(&bundle, NULL);

    qtest_memset(q, RAM, 0xa5, 0x400);
    pipeline_configure(q, RAM, 0x100, 0x200, 0x00020002);
    pipeline_request(q);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "rejected"), ==, 1);
    g_assert_cmphex(qtest_readl(q, RAM), ==, 0xa5a5a5a5);

    pipeline_configure(q, RAM, 0x80, 0x200,
                       NIKON_READOUT_C170_FORMAT_ALTERNATE);
    pipeline_request(q);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "rejected"), ==, 2);

    pipeline_configure(q, 0xfffffff0, 0x100, 0x200,
                       NIKON_READOUT_C170_FORMAT_PRIMARY);
    pipeline_request(q);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "rejected"), ==, 3);

    pipeline_configure(q, 0x10020000, 0x100, 0x200,
                       NIKON_READOUT_C170_FORMAT_PRIMARY);
    pipeline_request(q);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "rejected"), ==, 4);
    pipeline_configure(q, RAM, 0x100, 0x200,
                       NIKON_READOUT_C170_FORMAT_PRIMARY);
    qtest_writel(q, READOUT_PARAMETERS + 0x3c0, 0);
    pipeline_request(q);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "rejected"), ==, 5);
    qtest_writel(q, READOUT_PARAMETERS + 0x3c0, 1);
    qtest_writel(q, READOUT_PARAMETERS + 0x394, 2U << 16);
    pipeline_request(q);
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
    QTestState *q = pipeline_start(&bundle, NULL);

    qtest_memset(q, RAM, 0xa5, 0x200);
    pipeline_configure(q, RAM, 0x100, 0x200,
                       NIKON_READOUT_C170_FORMAT_PRIMARY);
    pipeline_request(q);
    pipeline_request(q);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "accepted"), ==, 1);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "rejected"), ==, 1);
    qtest_clock_step(q, 10000);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "stale"), ==, 0);
    g_assert_cmpuint(property_at(q, READOUT_PATH, "completion-generation"),
                     ==, 1);

    pipeline_request(q);
    qtest_clock_step(q, 10000);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "completed"), ==, 2);
    g_assert_cmpuint(property_at(q, READOUT_PATH, "completion-generation"),
                     ==, 2);
    qtest_quit(q);
    g_assert_cmpint(unlink(bundle), ==, 0);
}

static void statistics_rise(QTestState *q)
{
    qtest_set_irq_in(q, "/machine/capture-statistics", "automatic-rise", 0, 0);
    qtest_set_irq_in(q, "/machine/capture-statistics", "automatic-rise", 0, 1);
}

static void statistics_configure(QTestState *q)
{
    static const uint32_t dimensions[] = { 0x278, 0x2d0, 0x2e8, 0x300, 0x318 };

    for (unsigned i = 0; i < ARRAY_SIZE(dimensions); i++) {
        qtest_writel(q, 0x2006a000 + dimensions[i], 0x204);
    }
    qtest_writel(q, 0x2006a338, (2 << 10) | 4);
    qtest_writel(q, 0x2006a354, (2 << 11) | 4);
    qtest_writel(q, 0x2006a370, (2 << 10) | 4);
    for (unsigned n = 0; n < 10; n++) {
        uint32_t port = 0x2006a400 + n * 0x100;
        uint32_t base = RAM + 0x100000 + n * 0x10000;

        qtest_writel(q, port + 0x14, 3);
        qtest_writel(q, port + 0x18, 1);
        qtest_writel(q, port + 0x40, 1);
        qtest_writel(q, port + 0x44, 1);
        qtest_writel(q, port + 0x48, 2);
        qtest_writel(q, port + 0x80, 0x1000);
        for (unsigned bank = 0; bank < 2; bank++) {
            unsigned addresses = (n >= 2 && n <= 4) || n == 9 ? 4 : 1;

            for (unsigned a = 0; a < addresses; a++) {
                qtest_writel(q, port + 0x50 + bank * 16 + a * 4, base);
            }
        }
        qtest_memset(q, base, 0xa5, 0x3000);
    }
    qtest_writel(q, CAPTURE + 0x10, 0xd2);
    qtest_writel(q, CAPTURE + 8, 0xd2);
    qtest_writel(q, CAPTURE + 0x100, 8);
    qtest_writel(q, CAPTURE + 0x104, 8);
    qtest_writel(q, CAPTURE + 0x200, 0x40);
}

static void statistics_input(QTestState *q, uint8_t *bytes)
{
    pipeline_configure(q, RAM, 0x100, 0x400,
                       NIKON_READOUT_C170_FORMAT_PRIMARY);
    pipeline_request(q);
    qtest_clock_step(q, 10000);
    if (bytes) {
        qtest_memread(q, RAM, bytes, 0x400);
    }
}

static uint64_t statistics_sum(const uint8_t *bytes, unsigned x0, unsigned y0,
                                unsigned width, unsigned height)
{
    uint64_t sum = 0;

    for (unsigned y = y0; y < y0 + height; y++) {
        for (unsigned x = x0; x < x0 + width; x++) {
            sum += lduw_le_p(bytes + y * 256 + x * 2);
        }
    }
    return sum;
}

static void test_statistics_stage_completion(void)
{
    g_autofree char *bundle = NULL;
    QTestState *q = pipeline_start(&bundle, NULL);

    qtest_irq_intercept_out_named(q, "/machine/capture-control", "sysbus-irq");
    statistics_configure(q);
    qtest_writel(q, CAPTURE + 0x260, 0x20);
    statistics_rise(q);
    qtest_clock_step(q, 2000000);
    g_assert_cmphex(qtest_readl(q, CAPTURE + 0x270), ==, 0);
    g_assert_false(qtest_get_irq(q, NIKON_CAPTURE_THIRD_EVENT));

    statistics_input(q, NULL);
    statistics_rise(q);
    qtest_clock_step(q, 999999);
    g_assert_cmphex(qtest_readl(q, RAM + 0x101000), ==, 0xa5a5a5a5);
    g_assert_cmphex(qtest_readl(q, CAPTURE + 0x270), ==, 0);
    qtest_clock_step(q, 1);
    g_assert_cmphex(qtest_readl(q, RAM + 0x101000), !=, 0xa5a5a5a5);
    g_assert_cmphex(qtest_readl(q, 0x2006a110), ==, 128);
    g_assert_cmphex(qtest_readl(q, CAPTURE + 0x270), ==, 0x20);
    g_assert_cmphex(qtest_readl(q, CAPTURE + 0x290), ==, 0);
    g_assert_true(qtest_get_irq(q, NIKON_CAPTURE_THIRD_EVENT));

    /* Masking and W1C retain the existing independent data/stage causes. */
    qtest_writel(q, CAPTURE + 0x260, 0);
    g_assert_false(qtest_get_irq(q, NIKON_CAPTURE_THIRD_EVENT));
    g_assert_cmphex(qtest_readl(q, CAPTURE + 0x270), ==, 0x20);
    qtest_writel(q, CAPTURE + 0x260, 0x20);
    g_assert_true(qtest_get_irq(q, NIKON_CAPTURE_THIRD_EVENT));
    qtest_writel(q, CAPTURE + 0x270, 0x20);
    g_assert_false(qtest_get_irq(q, NIKON_CAPTURE_THIRD_EVENT));
    g_assert_cmphex(qtest_readl(q, CAPTURE + 0x210) & 0x40, ==, 0x40);

    /* Neither a new commit nor a blocked/failed/cancelled job replays it. */
    qtest_writel(q, CAPTURE + 0x100, 8);
    statistics_input(q, NULL);
    statistics_rise(q);
    qtest_clock_step(q, 2000000);
    g_assert_cmphex(qtest_readl(q, CAPTURE + 0x270), ==, 0);
    qtest_writel(q, CAPTURE + 0x210, 0x40);
    qtest_writel(q, 0x2006a450, 0x10020000);
    qtest_writel(q, CAPTURE + 0x100, 8);
    statistics_rise(q);
    qtest_clock_step(q, 2000000);
    g_assert_cmphex(qtest_readl(q, CAPTURE + 0x270), ==, 0);
    g_assert_cmpuint(property_at(q, "/machine/capture-statistics",
                                 "rejected"), ==, 1);
    qtest_writel(q, 0x2006a450, RAM + 0x100000);
    qtest_writel(q, CAPTURE + 0x100, 8);
    statistics_input(q, NULL);
    statistics_rise(q);
    qtest_writel(q, CAPTURE + 0xc, 0x10);
    qtest_clock_step(q, 2000000);
    g_assert_cmphex(qtest_readl(q, CAPTURE + 0x270), ==, 0);
    g_assert_cmpuint(property_at(q, "/machine/capture-statistics",
                                 "cancelled"), ==, 1);
    qtest_writel(q, CAPTURE + 8, 0x10);
    statistics_input(q, NULL);
    statistics_rise(q);
    qtest_clock_step(q, 2000000);
    g_assert_cmphex(qtest_readl(q, CAPTURE + 0x270), ==, 0x20);
    qtest_system_reset(q);
    g_assert_cmphex(qtest_readl(q, CAPTURE + 0x270), ==, 0);
    g_assert_false(qtest_get_irq(q, NIKON_CAPTURE_THIRD_EVENT));
    qtest_clock_step(q, 2000000);
    g_assert_cmphex(qtest_readl(q, CAPTURE + 0x270), ==, 0);
    statistics_configure(q);
    statistics_input(q, NULL);
    statistics_rise(q);
    qtest_system_reset(q);
    qtest_clock_step(q, 2000000);
    g_assert_cmphex(qtest_readl(q, CAPTURE + 0x270), ==, 0);
    qtest_quit(q);
    unlink(bundle);
}

static void test_statistics_storage_period(void)
{
    static const unsigned slots[] = { 6, 255, 6, 6, 6, 4, 4, 4, 4, 4 };
    g_autofree char *bundle = NULL;
    QTestState *q = pipeline_start(&bundle, NULL);

    statistics_configure(q);
    qtest_writel(q, CAPTURE + 0x200, 0);
    qtest_writel(q, CAPTURE + 0x260, 0x80);
    for (unsigned n = 0; n < ARRAY_SIZE(slots); n++) {
        uint32_t port = 0x2006a400 + n * 0x100;
        uint32_t base = RAM + (n + 1) * 0x100000;

        qtest_writel(q, port + 0x44, 0);
        qtest_writel(q, port + 0x48, 254);
        for (unsigned bank = 0; bank < 2; bank++) {
            unsigned addresses = (n >= 2 && n <= 4) || n == 9 ? 4 : 1;

            for (unsigned a = 0; a < addresses; a++) {
                qtest_writel(q, port + 0x50 + bank * 16 + a * 4, base);
            }
        }
        qtest_memset(q, base, 0xa5, (slots[n] + 1) * 0x1000);
    }
    qtest_writel(q, CAPTURE + 0x100, 8);
    for (unsigned frame = 0; frame < 255; frame++) {
        for (unsigned n = 0; n < ARRAY_SIZE(slots); n++) {
            if (frame == slots[n]) {
                qtest_writel(q, RAM + (n + 1) * 0x100000, 0xa5a5a5a5);
            }
        }
        statistics_input(q, NULL);
        statistics_rise(q);
        qtest_clock_step(q, 1000000);
        g_assert_cmpuint(property_at(q, "/machine/capture-statistics",
                                     "completed"), ==, frame + 1);
        g_assert_cmphex(qtest_readl(q, CAPTURE + 0x270) & 0x80, ==,
                        frame == 254 ? 0x80 : 0);
        for (unsigned n = 0; n < ARRAY_SIZE(slots); n++) {
            uint32_t base = RAM + (n + 1) * 0x100000;

            if (frame == slots[n]) {
                /* A real new result replaces the first slot on wrap. */
                g_assert_cmphex(qtest_readl(q, base), !=, 0xa5a5a5a5);
            }
            g_assert_cmphex(qtest_readl(q, base + slots[n] * 0x1000), ==,
                            0xa5a5a5a5);
        }
    }
    g_assert_cmpuint(property_at(q, "/machine/capture-statistics",
                                 "rejected"), ==, 0);
    /* The finite model restarts storage phase with the completion sequence. */
    qtest_writel(q, CAPTURE + 0x270, 0x80);
    for (unsigned n = 0; n < ARRAY_SIZE(slots); n++) {
        qtest_writel(q, RAM + (n + 1) * 0x100000, 0xa5a5a5a5);
    }
    statistics_input(q, NULL);
    statistics_rise(q);
    qtest_clock_step(q, 1000000);
    g_assert_cmpuint(property_at(q, "/machine/capture-statistics",
                                 "completed"), ==, 256);
    g_assert_cmphex(qtest_readl(q, CAPTURE + 0x270) & 0x80, ==, 0);
    for (unsigned n = 0; n < ARRAY_SIZE(slots); n++) {
        uint32_t base = RAM + (n + 1) * 0x100000;

        g_assert_cmphex(qtest_readl(q, base), !=, 0xa5a5a5a5);
        g_assert_cmphex(qtest_readl(q, base + slots[n] * 0x1000), ==,
                        0xa5a5a5a5);
    }
    qtest_quit(q);
    g_assert_cmpint(unlink(bundle), ==, 0);
}

static void test_statistics_ring_completion(void)
{
    g_autofree char *bundle = NULL;
    QTestState *q = pipeline_start(&bundle, NULL);

    qtest_irq_intercept_out_named(q, "/machine/capture-control", "sysbus-irq");
    statistics_configure(q);
    qtest_writel(q, CAPTURE + 0x260, 0x80);
    /* Two remaining slots in the three-slot endpoint1 ring. */
    for (unsigned i = 0; i < 2; i++) {
        statistics_input(q, NULL);
        statistics_rise(q);
        qtest_clock_step(q, 999999);
        g_assert_cmphex(qtest_readl(q, CAPTURE + 0x270) & 0x80, ==, 0);
        qtest_clock_step(q, 1);
        g_assert_cmphex(qtest_readl(q, CAPTURE + 0x270) & 0x80,
                        ==, i ? 0x80 : 0);
        g_assert_cmphex(qtest_readl(q, RAM + 0x111000 + i * 0x1000),
                        !=, 0xa5a5a5a5);
        qtest_writel(q, CAPTURE + 0x210, 0x40);
    }
    g_assert_true(qtest_get_irq(q, NIKON_CAPTURE_THIRD_EVENT));
    qtest_writel(q, CAPTURE + 0x270, 0x80);
    g_assert_false(qtest_get_irq(q, NIKON_CAPTURE_THIRD_EVENT));

    /* The original mode1 consumer selects bank1 and a fixed destination. */
    qtest_writel(q, 0x2006a53c, 1);
    qtest_writel(q, 0x2006a560, RAM + 0x200000);
    qtest_writel(q, 0x2006a580, 0);
    qtest_writel(q, 0x2006a544, 0);
    qtest_writel(q, 0x2006a548, 0);
    qtest_memset(q, RAM + 0x200000, 0xa5, 0x1000);
    qtest_writel(q, CAPTURE + 0x100, 8);
    g_assert_cmphex(qtest_readl(q, CAPTURE + 0x270) & 0x80, ==, 0);
    statistics_input(q, NULL);
    statistics_rise(q);
    qtest_clock_step(q, 1000000);
    g_assert_cmphex(qtest_readl(q, RAM + 0x200000), !=, 0xa5a5a5a5);
    g_assert_cmphex(qtest_readl(q, CAPTURE + 0x270) & 0x80, ==, 0x80);

    qtest_writel(q, CAPTURE + 0x270, 0x80);
    qtest_writel(q, CAPTURE + 0x210, 0x40);
    statistics_input(q, NULL);
    statistics_rise(q);
    qtest_writel(q, CAPTURE + 0xc, 0x10);
    qtest_clock_step(q, 1000000);
    g_assert_cmphex(qtest_readl(q, CAPTURE + 0x270) & 0x80, ==, 0);
    qtest_writel(q, CAPTURE + 8, 0x10);
    qtest_writel(q, 0x2006a560, 0x10020000);
    qtest_writel(q, CAPTURE + 0x100, 8);
    statistics_input(q, NULL);
    statistics_rise(q);
    qtest_clock_step(q, 1000000);
    g_assert_cmphex(qtest_readl(q, CAPTURE + 0x270) & 0x80, ==, 0);
    qtest_system_reset(q);
    qtest_clock_step(q, 1000000);
    g_assert_cmphex(qtest_readl(q, CAPTURE + 0x270), ==, 0);
    qtest_quit(q);
    unlink(bundle);
}

static void main_input_rise(QTestState *q)
{
    qtest_set_irq_in(q, PIPELINE_PATH, "automatic-rise", 0, 0);
    qtest_set_irq_in(q, PIPELINE_PATH, "automatic-rise", 0, 1);
}

static void main_input_configure(QTestState *q)
{
    qtest_writel(q, CAPTURE + 0x1d00, 4 << 16);
    qtest_writel(q, CAPTURE + 0x1d04, 256 << 16);
    qtest_writel(q, CAPTURE + 0x1e78, 4 << 16);
    qtest_writel(q, CAPTURE + 0x1e7c, 128 << 16);
    qtest_writel(q, CAPTURE + 0x1f00, 0x11);
    qtest_writel(q, CAPTURE + 0x100, 5);
    qtest_writel(q, CAPTURE + 0xc, 0xc2);
    qtest_writel(q, CAPTURE + 0x14, 0xc2);
    qtest_writel(q, CAPTURE + 0x10, 0x1d);
    qtest_writel(q, CAPTURE + 8, 0x1d);
}

static void test_main_input(void)
{
    g_autofree char *bundle = NULL;
    QTestState *q = pipeline_start(&bundle, NULL);

    qtest_qmp_assert_success(q,
        "{'execute':'qom-set','arguments':{'path':'/machine/cmos',"
        "'property':'image','value':%s}}", source);
    statistics_configure(q);
    main_input_configure(q);
    qtest_memset(q, RAM, 0xa5, 0x400);
    main_input_rise(q);
    qtest_clock_step(q, 10000);
    /* Main run1d has no auxiliary request; it still owns a new scene frame. */
    g_assert_cmpuint(property_at(q, "/machine/cmos", "frame-count"), ==, 1);
    g_assert_cmphex(qtest_readl(q, CAPTURE + 0x210) & 1, ==, 1);
    g_assert_cmphex(qtest_readl(q, RAM), ==, 0xa5a5a5a5);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "requests"), ==, 0);
    /* Cancelling an in-flight auxiliary DMA preserves the delivered main frame. */
    pipeline_configure(q, RAM, 256, 1024, NIKON_READOUT_C170_FORMAT_PRIMARY);
    qtest_writel(q, CAPTURE + 0x10, 2);
    qtest_writel(q, CAPTURE + 8, 2);
    pipeline_request(q);
    qtest_writel(q, CAPTURE + 0xc, 2);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "cancelled"), ==, 1);
    statistics_rise(q);
    qtest_clock_step(q, 1000000);
    g_assert_cmpuint(property_at(q, "/machine/capture-statistics",
                                 "completed"), ==, 1);
    static const unsigned expected[] = { 0, 64 * 257, 128 * 257, 65535 };
    for (unsigned i = 0; i < 4; i++) {
        g_assert_cmpuint(qtest_readl(q, 0x2006a3e0 + 4 * i), ==, expected[i]);
    }
    qtest_quit(q);
    unlink(bundle);
}

static void test_main_input_lifecycle(void)
{
    static const unsigned units[] = { 1, 4, 8 };
    g_autofree char *bundle = NULL;
    QTestState *q = pipeline_start(&bundle, NULL);

    main_input_configure(q);
    qtest_clock_step(q, 10000);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "main-completed"), ==, 0);
    main_input_rise(q);
    qtest_set_irq_in(q, PIPELINE_PATH, "cmos-event",
                     NIKON_CMOS_PIPELINE_DONE, 1);
    qtest_set_irq_in(q, PIPELINE_PATH, "cmos-event",
                     NIKON_CMOS_PIPELINE_DONE, 0);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "main-completed"), ==, 0);
    /* An auxiliary request may queue, but cancelling it cannot cancel main. */
    pipeline_configure(q, RAM, 256, 1024, NIKON_READOUT_C170_FORMAT_PRIMARY);
    qtest_writel(q, CAPTURE + 0x10, 2);
    qtest_writel(q, CAPTURE + 8, 2);
    qtest_writel(q, READOUT_PARAMETERS + 0x400, 1);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "accepted"), ==, 1);
    qtest_writel(q, CAPTURE + 0xc, 2);
    qtest_clock_step(q, 10000);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "main-completed"), ==, 1);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "main-cancelled"), ==, 0);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "completed"), ==, 0);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "cancelled"), ==, 1);
    main_input_rise(q);
    qtest_clock_step(q, 10000);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "main-completed"), ==, 2);
    qtest_writel(q, CAPTURE + 0x210, 1);
    main_input_rise(q);
    /* Equal commits preserve work; staging alone does not change this frame. */
    qtest_writel(q, CAPTURE + 0x104, 5);
    qtest_writel(q, CAPTURE + 0x1e7c, 64 << 16);
    qtest_clock_step(q, 10000);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "main-completed"), ==, 3);
    qtest_writel(q, CAPTURE + 0x210, 1);
    main_input_rise(q);
    qtest_writel(q, CAPTURE + 0x100, 5);
    qtest_clock_step(q, 10000);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "main-completed"), ==, 3);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "main-cancelled"), ==, 1);

    for (unsigned i = 0; i < ARRAY_SIZE(units); i++) {
        main_input_rise(q);
        qtest_writel(q, CAPTURE + 0xc, units[i]);
        qtest_clock_step(q, 10000);
        g_assert_cmpuint(property_at(q, PIPELINE_PATH, "main-completed"), ==, 3);
        qtest_writel(q, CAPTURE + 8, units[i]);
    }
    main_input_rise(q);
    qtest_system_reset(q);
    qtest_clock_step(q, 10000);
    g_assert_cmpuint(property_at(q, "/machine/cmos", "frame-count"), ==, 0);
    g_assert_cmphex(qtest_readl(q, CAPTURE + 0x210) & 1, ==, 0);
    main_input_configure(q);
    qtest_set_irq_in(q, PIPELINE_PATH, "automatic-rise", 0, 1);
    qtest_clock_step(q, 10000);
    g_assert_cmpuint(property_at(q, "/machine/cmos", "frame-count"), ==, 0);
    main_input_rise(q);
    qtest_clock_step(q, 10000);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "main-completed"), ==, 4);
    /* Queue auxiliary behind main; its DMA starts only on the next fresh TG. */
    main_input_rise(q);
    pipeline_configure(q, RAM, 256, 1024, NIKON_READOUT_C170_FORMAT_PRIMARY);
    qtest_writel(q, CAPTURE + 0x10, 2);
    qtest_writel(q, CAPTURE + 8, 2);
    qtest_writel(q, READOUT_PARAMETERS + 0x400, 1);
    qtest_clock_step(q, 10000);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "main-completed"), ==, 5);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "completed"), ==, 0);
    pipeline_input(q);
    /* Main stop while auxiliary DMA is active must leave that DMA owned. */
    qtest_writel(q, CAPTURE + 0xc, 1);
    qtest_clock_step(q, 10000);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "completed"), ==, 1);
    qtest_quit(q);
    unlink(bundle);
}

static void test_main_input_boundaries(void)
{
    static const uint32_t bad[][2] = {
        { 0x1d00, 1 | (4 << 16) },  /* nonzero source origin */
        { 0x1e7c, 0 },             /* empty output */
        { 0x1e7c, 512 << 16 },     /* expansion */
        { 0x1f00, 0 },             /* disabled geometry */
        { 0x500, 1 },              /* unknown correction arithmetic */
    };

    for (unsigned i = 0; i < ARRAY_SIZE(bad); i++) {
        g_autofree char *bundle = NULL;
        QTestState *q = pipeline_start(&bundle, NULL);

        main_input_configure(q);
        qtest_writel(q, CAPTURE + bad[i][0], bad[i][1]);
        qtest_writel(q, CAPTURE + 0x100, 5);
        main_input_rise(q);
        qtest_clock_step(q, 10000);
        g_assert_cmpuint(property_at(q, PIPELINE_PATH, "main-completed"), ==, 0);
        g_assert_cmpuint(property_at(q, PIPELINE_PATH, "main-rejected"), ==, 1);
        g_assert_cmphex(qtest_readl(q, CAPTURE + 0x210) & 1, ==, 0);
        qtest_quit(q);
        unlink(bundle);
    }
    g_autofree char *bundle = NULL;
    QTestState *q = pipeline_start(&bundle, NULL);

    main_input_configure(q);
    qtest_qmp_assert_success(q,
        "{'execute':'qom-set','arguments':{'path':'/machine/cmos',"
        "'property':'fixture-provider','value':false}}");
    main_input_rise(q);
    qtest_clock_step(q, 10000);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "main-rejected"), ==, 1);
    g_assert_cmphex(qtest_readl(q, CAPTURE + 0x210) & 1, ==, 0);
    qtest_quit(q);
    unlink(bundle);
}

static void test_main_input_masked_stream(void)
{
    static const unsigned dimensions[] = {
        0x278, 0x2d0, 0x2e8, 0x300, 0x318, 0x338, 0x354, 0x370,
    };
    g_autofree char *bundle = NULL;
    QTestState *q = pipeline_start(&bundle, NULL);

    statistics_configure(q);
    /* Mode1 disables these output geometries but retains endpoint routes. */
    qtest_writel(q, 0x2006a278, 0);
    qtest_writel(q, 0x2006a354, 0);
    qtest_writel(q, 0x2006a370, 0);
    qtest_writel(q, 0x2006a450, 0x10020000);
    qtest_writel(q, 0x2006a544, 0);
    qtest_writel(q, CAPTURE + 0x100, 8);
    main_input_configure(q);
    /* First-read mode1 has ring completion, without per-frame data ACKs. */
    qtest_writel(q, CAPTURE + 0x200, 0);
    qtest_writel(q, CAPTURE + 0x260, 0x80);
    for (unsigned i = 0; i < 3; i++) {
        main_input_rise(q);
        qtest_clock_step(q, 10000);
        statistics_rise(q);
        qtest_clock_step(q, 1000000);
        g_assert_cmpuint(property_at(q, "/machine/capture-statistics",
                                     "completed"), ==, i + 1);
        g_assert_cmphex(qtest_readl(q, CAPTURE + 0x270) & 0x80,
                        ==, i == 2 ? 0x80 : 0);
    }
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "requests"), ==, 0);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "main-completed"), ==, 3);
    g_assert_cmphex(qtest_readl(q, CAPTURE + 0x210) & 0x41, ==, 0x41);
    qtest_writel(q, CAPTURE + 0x270, 0xa0);
    for (unsigned i = 0; i < ARRAY_SIZE(dimensions); i++) {
        qtest_writel(q, 0x2006a000 + dimensions[i], 0);
    }
    qtest_writel(q, CAPTURE + 0x100, 8);
    main_input_rise(q);
    qtest_clock_step(q, 10000);
    statistics_rise(q);
    qtest_clock_step(q, 1000000);
    g_assert_cmpuint(property_at(q, "/machine/capture-statistics",
                                 "completed"), ==, 3);
    g_assert_cmpuint(property_at(q, "/machine/capture-statistics",
                                 "rejected"), ==, 1);
    g_assert_cmphex(qtest_readl(q, CAPTURE + 0x270), ==, 0);
    qtest_quit(q);
    unlink(bundle);
}

static void test_statistics_transaction(void)
{
    g_autofree char *bundle = NULL;
    QTestState *q = pipeline_start(&bundle, NULL);
    uint8_t input[0x400];

    /* The old model fails here: no capture processing device exists. */
    g_assert_cmpuint(property_at(q, "/machine/capture-statistics",
                                 "completed"), ==, 0);
    statistics_configure(q);
    qtest_writel(q, 0x2006a53c, 3);
    qtest_writel(q, 0x2006a560, RAM + 0x114000);
    qtest_memset(q, RAM + 0x114000, 0xa5, 0x3000);
    qtest_writel(q, CAPTURE + 0x100, 8);
    statistics_rise(q);
    qtest_clock_step(q, 2000000);
    g_assert_cmpuint(property_at(q, "/machine/capture-statistics",
                                 "completed"), ==, 0);
    statistics_input(q, input);
    qtest_memset(q, RAM, 0x55, sizeof(input));
    statistics_rise(q);
    /* Live staging and a newer CMOS completion cannot alter this job. */
    qtest_writel(q, 0x2006a560, RAM + 0x200000);
    statistics_input(q, NULL);
    /* Returning to the same bytes still establishes a newer commit. */
    qtest_writel(q, CAPTURE + 0x100, 8);
    qtest_writel(q, 0x2006a560, RAM + 0x114000);
    qtest_writel(q, CAPTURE + 0x100, 8);
    qtest_clock_step(q, 2000000);
    g_assert_cmpuint(property_at(q, "/machine/capture-statistics",
                                 "completed"), ==, 1);
    g_assert_cmphex(qtest_readl(q, CAPTURE + 0x210) & 0x40, ==, 0x40);
    for (unsigned n = 0; n < 10; n++) {
        uint32_t base = RAM + 0x101000 + n * 0x10000;
        unsigned stride = n ? 16 : 64;

        if (n == 1) {
            base = RAM + 0x115000;
        }
        g_assert_cmphex(qtest_readl(q, base - 4), ==, 0xa5a5a5a5);
        g_assert_cmphex(qtest_readl(q, base + stride * 2), ==, 0xa5a5a5a5);
        for (unsigned y = 0; y < 2; y++) {
            for (unsigned x = 0; x < 4; x++) {
                unsigned mean =
                    (statistics_sum(input, x * 32, y * 2, 32, 2) + 32) / 64;

                if (!n) {
                    g_assert_cmphex(qtest_readl(q, base + y * stride +
                                                x * 16 + 12), ==, mean);
                    g_assert_cmphex(qtest_readl(q, base + y * stride +
                                                x * 16 + 8), ==, 64);
                } else if (n <= 4) {
                    g_assert_cmphex(qtest_readw(q, base + y * stride + x * 2),
                                    ==, mean);
                } else {
                    g_assert_cmphex(qtest_readb(q, base + y * stride + x),
                                    ==, mean >> 8);
                }
            }
        }
    }
    for (unsigned bank = 0; bank < 2; bank++) {
        uint32_t base = bank ? 0x2006bf00 : 0x2006a100;

        for (unsigned i = 0; i < 4; i++) {
            unsigned x = (i % 2) * 64, y = (i / 2) * 2;
            uint64_t sum = statistics_sum(input, x, y, 64, 2);
            int64_t difference =
                (int64_t)statistics_sum(input, x + 63, y, 1, 2) -
                (int64_t)statistics_sum(input, x, y, 1, 2);
            unsigned index = i < 2 ? i : i + 4;

            g_assert_cmphex(qtest_readl(q, base + index * 4),
                            ==, difference & 0xffffff);
            g_assert_cmphex(qtest_readl(q, base + (index + 2) * 4), ==, sum);
            g_assert_cmphex(qtest_readl(q, base + (index + 4) * 4), ==, 128);
            g_assert_cmphex(qtest_readl(q, 0x2006a3e0 + i * 4),
                            ==, (sum + 64) / 128);
        }
    }
    /* ACK backpressure and duplicate commits preserve the published packet. */
    qtest_writel(q, CAPTURE + 0x100, 8);
    qtest_writel(q, CAPTURE + 0x104, 8);
    statistics_rise(q);
    qtest_clock_step(q, 2000000);
    g_assert_cmpuint(property_at(q, "/machine/capture-statistics",
                                 "completed"), ==, 1);
    g_assert_cmpuint(property_at(q, "/machine/capture-statistics",
                                 "blocked"), >, 0);
    qtest_writel(q, CAPTURE + 0x210, 0x40);
    statistics_rise(q);
    qtest_clock_step(q, 2000000);
    g_assert_cmpuint(property_at(q, "/machine/capture-statistics",
                                 "completed"), ==, 2);
    /* The newer A snapshot starts at slot1; the old job cannot advance it. */
    g_assert_cmphex(qtest_readl(q, RAM + 0x102000), ==, 0xa5a5a5a5);
    g_assert_cmphex(qtest_readl(q, RAM + 0x116000), ==, 0xa5a5a5a5);
    qtest_quit(q);
    unlink(bundle);
}

static void test_statistics_saturation(void)
{
    static const char header[] = "P5\n128 8192\n255\n";
    const size_t pixels = 128 * 8192, prefix = sizeof(header) - 1;
    g_autofree char *bundle = NULL;
    g_autofree char *path = NULL;
    g_autofree uint8_t *image = g_malloc(prefix + pixels);
    QTestState *q = pipeline_start(&bundle, NULL);
    int fd = g_file_open_tmp("nikon-statistics-XXXXXX", &path, NULL);

    g_assert_cmpint(fd, >=, 0);
    close(fd);
    memcpy(image, header, prefix);
    memset(image + prefix, 255, pixels);
    for (unsigned y = 0; y < 8192; y++) {
        image[prefix + y * 128] = 0;
        image[prefix + y * 128 + 127] = 0;
    }
    g_assert_true(g_file_set_contents(path, (char *)image,
                                     prefix + pixels, NULL));
    qtest_qmp_assert_success(q,
        "{'execute':'qom-set','arguments':{'path':'/machine/cmos',"
        "'property':'image','value':%s}}", path);
    statistics_configure(q);
    pipeline_configure(q, RAM + 0x400000, 256, 256 * 8192,
                       NIKON_READOUT_C170_FORMAT_PRIMARY);
    pipeline_request(q);
    qtest_clock_step(q, 100000);
    statistics_rise(q);
    qtest_clock_step(q, 2000000);
    g_assert_cmpuint(property_at(q, "/machine/capture-statistics",
                                 "completed"), ==, 1);
    for (unsigned i = 0; i < 4; i++) {
        unsigned base = (i < 2 ? i : i + 4) * 4 + 0x2006bf00;

        g_assert_cmphex(qtest_readl(q, base), ==, i % 2 ? 0x800000 : 0x7fffff);
        g_assert_cmphex(qtest_readl(q, base + 8), ==, 0x1fffffff);
        g_assert_cmphex(qtest_readl(q, base + 16), ==, 0x1ffff);
        g_assert_cmphex(qtest_readl(q, 0x2006a3e0 + i * 4), ==,
                        (65535u * 63 + 32) / 64);
    }
    qtest_quit(q);
    unlink(bundle);
    unlink(path);
}

static void test_statistics_lifecycle(void)
{
    g_autofree char *bundle = NULL;
    QTestState *q = pipeline_start(&bundle, NULL);

    statistics_configure(q);
    statistics_input(q, NULL);
    statistics_rise(q);
    qtest_clock_step(q, 2000000);
    statistics_input(q, NULL);
    statistics_rise(q);
    qtest_clock_step(q, 2000000);
    g_assert_cmpuint(property_at(q, "/machine/capture-statistics",
                                 "completed"), ==, 1);
    /* Empty original queues may discard a frame without any result reads. */
    qtest_writel(q, CAPTURE + 0x210, 0x40);
    statistics_rise(q);
    qtest_clock_step(q, 2000000);
    g_assert_cmpuint(property_at(q, "/machine/capture-statistics",
                                 "completed"), ==, 2);
    /* Advance once per success; unchanged dual commits do not rewind. */
    g_assert_cmphex(qtest_readl(q, RAM + 0x102000 + 8), ==, 64);
    qtest_writel(q, CAPTURE + 0x210, 0x40);
    for (unsigned i = 0; i < 12; i++) {
        qtest_readl(q, 0x2006bf00 + i * 4);
    }
    qtest_readl(q, 0x2006a3e0);
    statistics_input(q, NULL);
    statistics_rise(q);
    qtest_writel(q, CAPTURE + 0xc, 0x10);
    qtest_clock_step(q, 2000000);
    g_assert_cmpuint(property_at(q, "/machine/capture-statistics",
                                 "completed"), ==, 2);
    g_assert_cmpuint(property_at(q, "/machine/capture-statistics",
                                 "cancelled"), ==, 1);
    g_assert_cmphex(qtest_readl(q, CAPTURE + 0x210) & 0x40, ==, 0);
    /* Stop/restart must not revive the old job; bank1 alone preserves bank0. */
    for (unsigned bank = 0; bank < 2; bank++) {
        for (unsigned i = 0; i < 12; i++) {
            qtest_readl(q, (bank ? 0x2006bf00 : 0x2006a100) + i * 4);
        }
    }
    qtest_writel(q, CAPTURE + 8, 0xd2);
    statistics_input(q, NULL);
    statistics_rise(q);
    qtest_writel(q, CAPTURE + 0xc, 0x40);
    qtest_writel(q, CAPTURE + 8, 0xd2);
    qtest_clock_step(q, 2000000);
    g_assert_cmpuint(property_at(q, "/machine/capture-statistics",
                                 "cancelled"), ==, 2);
    g_assert_cmphex(qtest_readl(q, CAPTURE + 0x210) & 0x40, ==, 0);
    qtest_writel(q, CAPTURE + 0xc, 0x40);
    statistics_input(q, NULL);
    statistics_rise(q);
    qtest_clock_step(q, 2000000);
    g_assert_cmpuint(property_at(q, "/machine/capture-statistics",
                                 "completed"), ==, 3);
    g_assert_cmphex(property_at(q, "/machine/capture-banks", "read-mask0"),
                    ==, 0xfff);
    g_assert_cmphex(property_at(q, "/machine/capture-banks", "read-mask1"),
                    ==, 0);
    qtest_qmp_assert_success(q, "{'execute':'system_reset'}");
    qtest_qmp_eventwait(q, "RESET");
    statistics_rise(q);
    qtest_clock_step(q, 2000000);
    g_assert_cmphex(qtest_readl(q, CAPTURE + 0x210), ==, 0);
    qtest_quit(q);
    unlink(bundle);
}

static void test_statistics_boundaries(void)
{
    static const uint32_t bad[][2] = {
        { 0x2006ad60, 0x10020000 }, /* ROM */
        { 0x2006ad50, 0xfffffff0 }, /* overflow */
        { 0x2006ad54, RAM },        /* unmodeled address layout */
        { 0x2006ad80, 1 },          /* undersized slot */
        { 0x2006ad50, RAM + 0x100000 }, /* output overlap */
        { 0x2006ad3c, 6 },          /* unknown bank mode */
        { 0x2006a278, 4 },          /* incomplete geometry */
    };

    for (unsigned c = 0; c < ARRAY_SIZE(bad); c++) {
        g_autofree char *bundle = NULL;
        QTestState *q = pipeline_start(&bundle, NULL);

        statistics_configure(q);
        qtest_writel(q, bad[c][0], bad[c][1]);
        if (c == 0 || c == 1 || c == 4) {
            unsigned set = c == 0 ? 0x60 : 0x50;

            for (unsigned i = 1; i < 4; i++) {
                qtest_writel(q, 0x2006ad00 + set + i * 4, bad[c][1]);
            }
        }
        if (c == 0) {
            qtest_writel(q, 0x2006ad3c, 1);
        }
        qtest_writel(q, CAPTURE + 0x100, 8);
        statistics_input(q, NULL);
        statistics_rise(q);
        qtest_clock_step(q, 2000000);
        g_assert_cmpuint(property_at(q, "/machine/capture-statistics",
                                     "rejected"), ==, 1);
        g_assert_cmpuint(property_at(q, "/machine/capture-statistics",
                                     "completed"), ==, 0);
        g_assert_cmphex(qtest_readl(q, CAPTURE + 0x210) & 0x40, ==, 0);
        g_assert_cmphex(qtest_readl(q, RAM + 0x101000), ==, 0xa5a5a5a5);
        qtest_quit(q);
        unlink(bundle);
    }
}

static void test_pipeline_cancel_and_reset(void)
{
    g_autofree char *bundle = NULL;
    QTestState *q = pipeline_start(&bundle, NULL);

    qtest_memset(q, RAM, 0xa5, 0x200);
    pipeline_configure(q, RAM, 0x100, 0x200,
                       NIKON_READOUT_C170_FORMAT_PRIMARY);
    pipeline_request(q);
    qtest_writel(q, CMOS + 0x064, 1);
    qtest_clock_step(q, 10000);
    g_assert_cmphex(qtest_readl(q, RAM), ==, 0xa5a5a5a5);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "cancelled"), ==, 1);
    g_assert_cmpuint(property_at(q, READOUT_PATH, "completion-generation"),
                     ==, 0);

    pipeline_request(q);
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

#define WORK_PATH "/machine/capture-processing"
#define WORK_BUFFER (RAM + 0x100000)

static void work_rise(QTestState *q)
{
    qtest_set_irq_in(q, WORK_PATH, "automatic-rise", 0, 0);
    qtest_set_irq_in(q, WORK_PATH, "automatic-rise", 0, 1);
}

static void work_configure(QTestState *q, unsigned format, bool lookup)
{
    static const unsigned offsets[] = {
        0x208, 0x214, 0x218, 0x224, 0x23c, 0x240, 0x244, 0x248, 0x24c,
        0x250, 0x254, 0x258, 0x25c, 0x260, 0x264, 0x268, 0x26c, 0x280,
    };
    static const unsigned buffers[] = {
        16, 1, 1, 0, 0, 1, 1, 1, 0,
        WORK_BUFFER, WORK_BUFFER + 0x1000, 0, 0,
        WORK_BUFFER + 0x2000, WORK_BUFFER + 0x3000, 0, 0, 0,
    };
    uint32_t words[] = { (lookup ? 4 : 0) | format, 8 << 16, 2 << 16, 0, 0, 0, 2,
                         0x00200040 };

    qtest_qmp_assert_success(q,
        "{'execute':'qom-set','arguments':{'path':'/machine/cmos',"
        "'property':'image','value':%s}}", source);
    qtest_qmp_assert_success(q,
        "{'execute':'qom-set','arguments':{'path':'/machine/cmos',"
        "'property':'fixture-provider','value':false}}");
    qtest_writel(q, CAPTURE + 0x500, 0);
    qtest_writel(q, 0x20069d00, 4 << 16);
    qtest_writel(q, 0x20069d04, 16 << 16);
    qtest_writel(q, 0x20069e78, 2 << 16);
    qtest_writel(q, 0x20069e7c, 8 << 16);
    qtest_writel(q, 0x20069f00, 0x11);
    for (unsigned i = 0; i < ARRAY_SIZE(words) - !lookup; i++) {
        qtest_writel(q, 0x20069514 + 4 * i, words[i]);
    }
    for (unsigned i = 0; lookup && i < 128; i++) {
        qtest_writel(q, 0x20069600 + 4 * i,
                     6 * i | ((6 * i + 3) << 16));
    }
    for (unsigned i = 0; i < ARRAY_SIZE(offsets); i++) {
        qtest_writel(q, 0x20069800 + offsets[i], buffers[i]);
    }
    qtest_memset(q, WORK_BUFFER - 16, 0xa5, 0x4040);
    qtest_writel(q, CAPTURE + 0x10, 0x20f);
    qtest_writel(q, CAPTURE + 8, 0x20f);
    qtest_writel(q, CAPTURE + 0x100, 0x85);
    qtest_writel(q, CAPTURE + 0x104, 0x85);
}

static void work_input(QTestState *q)
{
    statistics_input(q, NULL);
    qtest_writel(q, CAPTURE + 0x210, qtest_readl(q, CAPTURE + 0x210));
}

static void work_ack(QTestState *q)
{
    qtest_writel(q, CAPTURE + 0x210, qtest_readl(q, CAPTURE + 0x210));
    qtest_writel(q, CAPTURE + 0x290, qtest_readl(q, CAPTURE + 0x290));
}

static void test_capture_processing(void)
{
    g_autofree char *bundle = NULL;
    QTestState *q = pipeline_start(&bundle, NULL);
    static const uint8_t expected[2][12] = {
        { 0, 0, 0, 0, 0, 0, 0xc0, 0, 0xc, 0xc0, 0, 0xc },
        { 0x80, 1, 0x18, 0x80, 1, 0x18, 0xfd, 0xd2, 0x2f,
          0xfd, 0xd2, 0x2f },
    };
    uint8_t row[16];

    g_assert_cmpuint(property_at(q, WORK_PATH, "completed"), ==, 0);
    work_configure(q, 2, true);
    g_assert_cmphex(qtest_readl(q, WORK_BUFFER + 0x100c), ==, 0xa5a5a5a5);
    work_rise(q);
    qtest_clock_step(q, 2000000);
    g_assert_cmpuint(property_at(q, WORK_PATH, "completed"), ==, 0);
    work_input(q);
    work_rise(q);
    /* Neither live RAM reuse nor uncommitted parameters change the job. */
    qtest_memset(q, RAM, 0x55, 0x400);
    qtest_writel(q, 0x20069a54, WORK_BUFFER + 0x3000);
    qtest_writel(q, 0x20069600, 0xffffffff);
    qtest_clock_step(q, 2000000);
    g_assert_cmpuint(property_at(q, WORK_PATH, "completed"), ==, 1);
    g_assert_cmpuint(property_at(q, WORK_PATH, "bytes"), ==, 24);
    for (unsigned y = 0; y < 2; y++) {
        qtest_memread(q, WORK_BUFFER + 0x1000 + 16 * y, row, 16);
        g_assert_cmpmem(row, 12, expected[y], 12);
        g_assert_cmphex(qtest_readl(q, WORK_BUFFER + 0x100c + 16 * y),
                        ==, 0xa5a5a5a5);
    }
    g_assert_cmphex(qtest_readl(q, WORK_BUFFER), ==, 0xa5a5a5a5);
    g_assert_cmphex(qtest_readl(q, WORK_BUFFER + 0x1020), ==, 0xa5a5a5a5);
    g_assert_cmphex(qtest_readl(q, CAPTURE + 0x210) & 0x18, ==, 0x18);
    g_assert_cmphex(qtest_readl(q, CAPTURE + 0x290), ==, 0x80);
    /* Group and unrelated processing causes have no producer in this job. */
    g_assert_cmphex(qtest_readl(q, CAPTURE + 0x270), ==, 0);
    work_ack(q);
    work_rise(q);
    qtest_clock_step(q, 2000000);
    g_assert_cmpuint(property_at(q, WORK_PATH, "completed"), ==, 1);
    qtest_quit(q);
    unlink(bundle);
}

static void test_capture_work_lifecycle(void)
{
    for (unsigned operation = 0; operation < 9; operation++) {
        g_autofree char *bundle = NULL;
        QTestState *q = pipeline_start(&bundle, NULL);

        work_configure(q, 2, true);
        work_input(q);
        work_rise(q);
        if (operation < 4) {
            static const uint32_t units[] = { 1, 4, 8, 0x200 };

            qtest_writel(q, CAPTURE + 0xc, units[operation]);
        } else if (operation == 4) {
            qtest_writel(q, CAPTURE, 0x200);
        } else if (operation == 5) {
            qtest_writel(q, CAPTURE + 0x14, 8);
        } else if (operation == 6) {
            qtest_writel(q, 0x20069514, 4);
            qtest_writel(q, CAPTURE + 0x104, 0x80);
        } else if (operation == 7) {
            qtest_writel(q, CAPTURE + 0x500, 1);
            qtest_writel(q, CAPTURE + 0x100, 1);
        } else {
            qtest_system_reset(q);
        }
        qtest_clock_step(q, 2000000);
        g_assert_cmpuint(property_at(q, WORK_PATH, "completed"), ==, 0);
        g_assert_cmpuint(property_at(q, WORK_PATH, "cancelled"), ==, 1);
        g_assert_cmphex(qtest_readl(q, WORK_BUFFER + 0x1000), ==, 0xa5a5a5a5);
        g_assert_cmphex(qtest_readl(q, CAPTURE + 0x210) & 0x18, ==, 0);
        g_assert_cmphex(qtest_readl(q, CAPTURE + 0x290) & 0x80, ==, 0);
        qtest_quit(q);
        unlink(bundle);
    }
}

static void test_capture_work_boundaries(void)
{
    static const uint32_t bad[][3] = {
        { CAPTURE + 0x500, 1, 1 },
        { 0x20069a08, 8, 0x80 },
        { 0x20069a54, 0x10020000, 0x80 }, /* ROM */
        { 0x20069a54, 0x20068000, 0x80 }, /* MMIO */
        { 0x20069a54, WORK_BUFFER, 0x80 }, /* Overlap */
        { 0x20069a54, 0xfffffff0, 0x80 }, /* Wrap */
        { 0x20069514, 7, 0x80 },         /* Unknown encoding */
        { 0x20069600, 0x1000, 0x80 },    /* LUT cannot fit12 bits */
        { 0x20069518, 7 << 16, 0x80 },   /* Inconsistent geometry */
        { 0x20069f00, 0, 4 },
    };

    for (unsigned i = 0; i < ARRAY_SIZE(bad); i++) {
        g_autofree char *bundle = NULL;
        QTestState *q = pipeline_start(&bundle, NULL);

        work_configure(q, 2, true);
        qtest_writel(q, bad[i][0], bad[i][1]);
        qtest_writel(q, CAPTURE + 0x100, bad[i][2]);
        work_input(q);
        work_rise(q);
        qtest_clock_step(q, 2000000);
        g_assert_cmpuint(property_at(q, WORK_PATH, "rejected"), ==, 1);
        g_assert_cmpuint(property_at(q, WORK_PATH, "completed"), ==, 0);
        g_assert_cmphex(qtest_readl(q, WORK_BUFFER + 0x1000), ==, 0xa5a5a5a5);
        g_assert_cmphex(qtest_readl(q, CAPTURE + 0x210) & 0x18, ==, 0);
        g_assert_cmphex(qtest_readl(q, CAPTURE + 0x290) & 0x80, ==, 0);
        qtest_quit(q);
        unlink(bundle);
    }
}

static void test_capture_work_formats(void)
{
    for (unsigned mode = 0; mode < 6; mode++) {
        g_autofree char *bundle = NULL;
        QTestState *q = pipeline_start(&bundle, NULL);
        unsigned format = mode % 3;
        unsigned bits = format == 0 ? 16 : format == 1 ? 14 : 12;
        bool lookup = mode < 3;

        work_configure(q, format, lookup);
        qtest_writel(q, 0x20069a3c, 2); /* Independent second address set. */
        qtest_writel(q, CAPTURE + 0x104, 0x80);
        for (unsigned frame = 0; frame < 3; frame++) {
            unsigned slot = (1 + frame) % 2;
            uint32_t address = WORK_BUFFER + 0x2000 + slot * 0x1000;
            uint8_t row[19] = { 0 };

            work_input(q);
            /* Repeated equal commits preserve ring position. */
            qtest_writel(q, CAPTURE + 0x100, 0x85);
            work_rise(q);
            qtest_clock_step(q, 2000000);
            for (unsigned y = 0; y < 2; y++) {
                qtest_memread(q, address + 16 * y, row, 16);
                for (unsigned x = 0; x < 8; x++) {
                    static const unsigned lut[2][2] = {
                        { 0, 192 }, { 384, 765 },
                    };
                    static const unsigned gray[2][2] = {
                        { 0, 0x4040 }, { 0x8080, 0xffff },
                    };
                    unsigned bit = x * bits;
                    unsigned byte = bit / 8;
                    uint32_t value = row[byte] | row[byte + 1] << 8 |
                                     row[byte + 2] << 16;
                    unsigned expected = lookup ? lut[y][x / 4] :
                                         gray[y][x / 4] >> (16 - bits);

                    g_assert_cmpuint((value >> (bit % 8)) & ((1u << bits) - 1),
                                     ==, expected);
                }
            }
            g_assert_cmpuint(property_at(q, WORK_PATH, "completed"),
                             ==, frame + 1);
            work_ack(q);
        }
        g_assert_cmphex(qtest_readl(q, WORK_BUFFER), ==, 0xa5a5a5a5);
        qtest_quit(q);
        unlink(bundle);
    }
}

static void test_capture_work_freshness(void)
{
    g_autofree char *bundle = NULL;
    QTestState *q = pipeline_start(&bundle, NULL);

    work_configure(q, 2, true);
    /* The CMOS source completed while unit1 was stopped: no stream input. */
    qtest_writel(q, CAPTURE + 0xc, 1);
    work_input(q);
    qtest_writel(q, CAPTURE + 8, 1);
    work_rise(q);
    qtest_clock_step(q, 2000000);
    g_assert_cmpuint(property_at(q, WORK_PATH, "completed"), ==, 0);
    /* A real input cause supplies backpressure, never a processing ACK. */
    statistics_input(q, NULL);
    work_rise(q);
    g_assert_cmpuint(property_at(q, WORK_PATH, "blocked"), ==, 1);
    work_ack(q);
    work_rise(q);
    qtest_clock_step(q, 2000000);
    g_assert_cmpuint(property_at(q, WORK_PATH, "completed"), ==, 1);
    work_ack(q);
    /* Stop/start cannot replay the delivered generation into the next slot. */
    qtest_writel(q, CAPTURE + 0xc, 0x200);
    qtest_writel(q, CAPTURE + 8, 0x200);
    work_rise(q);
    qtest_clock_step(q, 2000000);
    g_assert_cmpuint(property_at(q, WORK_PATH, "completed"), ==, 1);
    g_assert_cmphex(qtest_readl(q, WORK_BUFFER), ==, 0xa5a5a5a5);
    work_input(q);
    work_rise(q);
    qtest_clock_step(q, 2000000);
    g_assert_cmpuint(property_at(q, WORK_PATH, "completed"), ==, 2);
    qtest_quit(q);
    unlink(bundle);
}

#define PAIRED 0x20380000
#define PAIRED_PATH "/machine/paired-processing"
#define PAIRED_MAIN (RAM + 0x110000)
#define PAIRED_WORK (RAM + 0x120000)
#define PAIRED_GRID (RAM + 0x130000)

static void paired_matrix(QTestState *q, uint32_t base, unsigned offset,
                          const int coefficients[9], unsigned bits,
                          bool low_first)
{
    uint32_t mask = (1u << bits) - 1;

    qtest_writel(q, base + offset, coefficients[0]);
    for (unsigned i = 1; i < 9; i += 2) {
        uint32_t first = coefficients[i] & mask;
        uint32_t second = coefficients[i + 1] & mask;

        qtest_writel(q, base + offset + 2 * (i + 1),
                     low_first ? first | (second << 16) :
                                 (first << 16) | second);
    }
}

static void paired_configure(QTestState *q, unsigned table_mode, uint32_t base)
{
    static const struct {
        unsigned offset;
        uint32_t value;
    } controls[] = {
        { 0x40, 5 }, { 0x44, 0 }, { 0x48, (64 << 16) | 36 },
        { 0x4c, 1 }, { 0x84, 1 }, { 0x8c, 3 },
        { 0x98, 0x400 }, { 0x9c, 36 - 1 },
        { 0xa0, 3 }, { 0xa8, 4 }, { 0xb8, 1 }, { 0xbc, 1 },
        { 0xc4, 64 }, { 0xd4, 0x0c060202 }, { 0xd8, 9 },
        { 0xdc, 0x30300 }, { 0xe0, 0x158 }, { 0xf0, 0x8e }, { 0xf8, 1 },
        { 0x100, 1 }, { 0x108, 3 }, { 0x11c, 1 }, { 0x120, 1 },
        { 0x124, 1 }, { 0x12c, 1 }, { 0x204, (36 << 16) | 64 },
        { 0x30c, 96 }, { 0x394, 96 }, { 0x3d8, 2 },
        { 0x400, (4 << 16) | 28 }, { 0x404, 1 }, { 0x408, 1 },
        { 0x40c, (56 << 16) | 4 }, { 0x410, 1 },
        { 0x420, 1 }, { 0x428, 1 }, { 0x42c, 3 },
        { 0x470, 80 }, { 0x4c8, 64 },
        { 0xb14, 0x80000000 }, { 0xb60, 0x80000000 },
        { 0xb90, 0x80000000 },
    };
    static const unsigned offsets[] = {
        0x208, 0x214, 0x218, 0x224, 0x23c, 0x240, 0x244, 0x248, 0x24c,
        0x250, 0x254, 0x258, 0x25c, 0x260, 0x264, 0x268, 0x26c, 0x280,
    };
    static const unsigned buffers[] = {
        96, 1, 1, 0, 0, 1, 1, 3, 0,
        WORK_BUFFER, WORK_BUFFER + 0x1000, WORK_BUFFER + 0x2000,
        WORK_BUFFER + 0x3000, 0, 0, 0, 0, 0,
    };
    static const int a[9] = { 1024, 0, 0, 0, 512, 0, 0, 0, 256 };
    static const int b[9] = { 512, 0, 0, 0, 256, -256, -256, 256, 0 };
    static const int c[9] = { 1024, 0, 0, 0, 1024, 0, 0, 0, 1024 };

    qtest_writel(q, CAPTURE + 0x500, 0);
    qtest_writel(q, CAPTURE + 0x1d00, 36 << 16);
    qtest_writel(q, CAPTURE + 0x1d04, 64 << 16);
    qtest_writel(q, CAPTURE + 0x1e78, 36 << 16);
    qtest_writel(q, CAPTURE + 0x1e7c, 64 << 16);
    qtest_writel(q, CAPTURE + 0x1f00, 0x11);
    qtest_writel(q, 0x20069514, 2);
    qtest_writel(q, 0x20069518, 64 << 16);
    qtest_writel(q, 0x2006951c, 36 << 16);
    qtest_writel(q, 0x20069520, 0);
    qtest_writel(q, 0x20069524, 0);
    qtest_writel(q, 0x20069528, 0);
    qtest_writel(q, 0x2006952c, 2);
    for (unsigned i = 0; i < ARRAY_SIZE(offsets); i++) {
        qtest_writel(q, 0x20069800 + offsets[i], buffers[i]);
    }
    for (unsigned i = 0; i < ARRAY_SIZE(controls); i++) {
        qtest_writel(q, base + controls[i].offset, controls[i].value);
    }
    for (unsigned offset = 0x320; offset <= 0x34c; offset += 4) {
        qtest_writel(q, base + offset, 0);
    }
    for (unsigned offset = 0x4a8; offset <= 0x4c4; offset += 4) {
        qtest_writel(q, base + offset, 0);
    }
    for (unsigned phase = 0; phase < 4; phase++) {
        unsigned slot = (phase + 1) % 4;
        uint32_t work = PAIRED_WORK + (phase % 2) * 0x2000;
        uint32_t main = PAIRED_MAIN + (phase % 2) * 0x2000;

        qtest_writel(q, base + 0x310 + 4 * phase,
                     WORK_BUFFER + slot * 0x1000);
        qtest_writel(q, base + 0x218 + 4 * phase,
                     WORK_BUFFER + slot * 0x1000);
        qtest_writel(q, base + 0x398 + 4 * phase,
                     PAIRED_GRID + phase * 0x1000);
        qtest_writel(q, base + 0x430 + 4 * phase, work);
        qtest_writel(q, base + 0x440 + 4 * phase, work + 80 * 18);
        qtest_writel(q, base + 0x450 + 4 * phase, work);
        qtest_writel(q, base + 0x460 + 4 * phase, work + 80 * 18);
        qtest_writel(q, base + 0x488 + 4 * phase, main);
        qtest_writel(q, base + 0x498 + 4 * phase, main + 64 * 28);
    }
    /* Synthetic table values exercise coverage without copying factory data. */
    for (unsigned offset = table_mode == 2 ? 0x1440 : 0x1400;
         offset < (table_mode ? 0x1d38 : 0x1d34); offset += 4) {
        qtest_writel(q, base + offset, 0);
    }
    paired_matrix(q, base, 0xb00, a, 13, false);
    paired_matrix(q, base, 0xb64, b, 12, false);
    paired_matrix(q, base, 0xb94, c, 13, true);
    qtest_writel(q, base + 0xe8, UINT32_MAX);
    qtest_writel(q, CAPTURE + 0x10, 0x20f);
    qtest_writel(q, CAPTURE + 8, 0x20f);
    qtest_writel(q, CAPTURE + 0x100, 0x85);
    qtest_writel(q, CAPTURE + 0x104, 0x85);
}

static void paired_secondary(QTestState *q, uint32_t extra)
{
    for (unsigned i = 0; i < 4; i++) {
        uint32_t raw = qtest_readl(q, PAIRED + 0x310 + 4 * i) + extra;

        qtest_writel(q, PAIRED + 0x228 + 4 * i, raw);
        qtest_writel(q, PAIRED + 0x320 + 4 * i, raw);
        qtest_writel(q, PAIRED + 0x3a8 + 4 * i,
                     qtest_readl(q, PAIRED + 0x398 + 4 * i) + extra);
        qtest_writel(q, PAIRED + 0x4a8 + 4 * i,
                     qtest_readl(q, PAIRED + 0x488 + 4 * i) + extra);
        qtest_writel(q, PAIRED + 0x4b8 + 4 * i,
                     qtest_readl(q, PAIRED + 0x498 + 4 * i) + extra);
    }
}

static void paired_pixels(QTestState *q, uint32_t base, unsigned phase)
{
    uint8_t expected[0xce0], actual[sizeof(expected)];

    memset(expected, 0xa5, sizeof(expected));
    if (base == PAIRED_MAIN) {
        for (unsigned y = 0; y < 28; y++) {
            memset(expected + 16 + 64 * y, 129, 56);
        }
        for (unsigned y = 0; y < 14; y++) {
            for (unsigned x = 0; x < 56; x += 2) {
                expected[16 + 64 * (28 + y) + x] = 144;
                expected[16 + 64 * (28 + y) + x + 1] = 96;
            }
        }
    } else if (base == PAIRED_WORK) {
        for (unsigned y = 0; y < 18; y++) {
            for (unsigned x = 0; x < 32; x++) {
                expected[16 + 80 * y + 2 * x] = 8;
                expected[16 + 80 * y + 2 * x + 1] = 8;
                expected[16 + 80 * (18 + y) + 2 * x] = 0;
                expected[16 + 80 * (18 + y) + 2 * x + 1] = 0;
            }
        }
    } else {
        for (unsigned y = 0; y < 34; y++) {
            for (unsigned x = 0; x < 90; x += 3) {
                memcpy(expected + 16 + 96 * y + x, "\x08\x88\x80", 3);
            }
        }
    }
    qtest_memread(q, base + phase * (base == PAIRED_GRID ? 0x1000 : 0x2000)
                     - 16, actual, sizeof(actual));
    g_assert_cmpmem(actual, sizeof(actual), expected, sizeof(expected));
}

static QTestState *paired_start_profile(char **bundle, char **scene,
                                       unsigned table_mode, bool arm, bool quad)
{
    static const uint8_t gray[] = {
        'P', '5', '\n', '1', ' ', '1', '\n', '2', '5', '5', '\n', 128,
    };
    uint8_t calibration[0x5e00] = { 0 };
    g_autofree char *profile = NULL;
    g_autofree char *quoted = NULL;
    g_autofree char *options = NULL;
    int fd = g_file_open_tmp("nikonpairedcalibrationXXXXXX", &profile, NULL);
    QTestState *q;

    g_assert_cmpint(fd, >=, 0);
    close(fd);
    stl_le_p(calibration + 0x700, quad ? 0x101 : 0);
    stl_le_p(calibration + 0xb14, 0x80000000);
    stl_le_p(calibration + 0xb60, 0x80000000);
    stl_le_p(calibration + 0xb90, 0x80000000);
    g_assert_true(g_file_set_contents(profile, (const char *)calibration,
                                      sizeof(calibration), NULL));
    quoted = g_shell_quote(profile);
    options = g_strdup_printf(",paired-calibration=%s", quoted);
    q = pipeline_start(bundle, options);
    /* Realization reads expected words without preloading guest registers. */
    g_assert_cmpint(unlink(profile), ==, 0);
    fd = g_file_open_tmp("nikonpairedimageXXXXXX", scene, NULL);

    g_assert_cmpint(fd, >=, 0);
    close(fd);
    g_assert_true(g_file_set_contents(*scene, (const char *)gray,
                                      sizeof(gray), NULL));
    qtest_qmp_assert_success(q,
        "{'execute':'qom-set','arguments':{'path':'/machine/cmos',"
        "'property':'image','value':%s}}", *scene);
    qtest_qmp_assert_success(q,
        "{'execute':'qom-set','arguments':{'path':'/machine/cmos',"
        "'property':'fixture-provider','value':false}}");
    nikon_intercept_gic(q);
    for (unsigned phase = 0; phase < 4; phase++) {
        qtest_memset(q, WORK_BUFFER + phase * 0x1000 - 16, 0xa5, 0xda0);
        qtest_memset(q, PAIRED_GRID + phase * 0x1000 - 16, 0xa5, 0xce0);
    }
    for (unsigned phase = 0; phase < 2; phase++) {
        qtest_memset(q, PAIRED_MAIN + phase * 0x2000 - 16, 0xa5, 0xce0);
        qtest_memset(q, PAIRED_WORK + phase * 0x2000 - 16, 0xa5, 0xce0);
    }
    paired_configure(q, table_mode, PAIRED);
    if (arm) {
        qtest_writel(q, PAIRED + 0x10, 1);
        qtest_writel(q, PAIRED + 8, 1);
    }
    return q;
}

static QTestState *paired_start(char **bundle, char **scene,
                                unsigned table_mode, bool arm)
{
    return paired_start_profile(bundle, scene, table_mode, arm, false);
}

static void paired_untouched(QTestState *q)
{
    uint8_t expected[0xce0], actual[sizeof(expected)];

    memset(expected, 0xa5, sizeof(expected));
    for (unsigned phase = 0; phase < 4; phase++) {
        qtest_memread(q, PAIRED_GRID + phase * 0x1000 - 16, actual,
                      sizeof(actual));
        g_assert_cmpmem(actual, sizeof(actual), expected, sizeof(expected));
    }
    for (unsigned phase = 0; phase < 2; phase++) {
        qtest_memread(q, PAIRED_MAIN + phase * 0x2000 - 16, actual,
                      sizeof(actual));
        g_assert_cmpmem(actual, sizeof(actual), expected, sizeof(expected));
        qtest_memread(q, PAIRED_WORK + phase * 0x2000 - 16, actual,
                      sizeof(actual));
        g_assert_cmpmem(actual, sizeof(actual), expected, sizeof(expected));
    }
    g_assert_cmphex(qtest_readl(q, PAIRED + 0x24), ==, 0);
    g_assert_false(qtest_get_irq(q, 99 - 32));
}

static void test_paired_processing(void)
{
    g_autofree char *bundle = NULL;
    g_autofree char *scene = NULL;
    QTestState *q = paired_start(&bundle, &scene, true, true);

    work_rise(q);
    qtest_clock_step(q, 2000000);
    g_assert_cmphex(qtest_readl(q, PAIRED_MAIN), ==, 0xa5a5a5a5);
    g_assert_cmphex(qtest_readl(q, PAIRED + 0x24), ==, 0);
    g_assert_false(qtest_get_irq(q, 99 - 32));
    for (unsigned phase = 0; phase < 4; phase++) {
        unsigned slot = (phase + 1) % 4;

        work_input(q);
        work_rise(q);
        qtest_clock_step(q, 1000000);
        g_assert_cmphex(qtest_readl(q, WORK_BUFFER + slot * 0x1000), ==,
                        0x08808808);
        qtest_clock_step(q, 1000000);
        /* First red oracle: an actual RAW DMA must produce real main pixels. */
        g_assert_cmphex(qtest_readb(q, PAIRED_MAIN + (phase % 2) * 0x2000),
                        ==, 129);
        paired_pixels(q, PAIRED_MAIN, phase % 2);
        paired_pixels(q, PAIRED_WORK, phase % 2);
        paired_pixels(q, PAIRED_GRID, phase);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==,
                         phase + 1);
        g_assert_cmphex(qtest_readl(q, PAIRED + 0x24), ==,
                        0x1002 | (phase == 3 ? 0x400 : 0));
        g_assert_false(qtest_get_irq(q, 99 - 32));
        qtest_writel(q, PAIRED + 0xe8, 0);
        g_assert_true(qtest_get_irq(q, 99 - 32));
        qtest_writel(q, PAIRED + 0x24, UINT32_MAX);
        g_assert_false(qtest_get_irq(q, 99 - 32));
        qtest_writel(q, PAIRED + 0xe8, UINT32_MAX);
        work_ack(q);
        work_rise(q);
        qtest_clock_step(q, 2000000);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==,
                         phase + 1);
    }
    g_assert_cmphex(qtest_readl(q, PAIRED + 0x118), ==, 4 * 56 * 28);
    qtest_writel(q, PAIRED + 0xc, 1);
    g_assert_cmphex(qtest_readl(q, PAIRED + 0x24), ==, 2);
    g_assert_cmphex(qtest_readl(q, PAIRED + 0x18), ==, 0);
    qtest_quit(q);
    unlink(scene);
    unlink(bundle);
}

static void test_paired_preflight(void)
{
    static const uint32_t targets[] = {
        0x10020000,       /* The last necessary grid is ROM. */
        PAIRED_GRID,      /* Independent grid phases must not overlap. */
        0xfffffff0,       /* The final grid extent cannot wrap PA32. */
    };

    for (unsigned i = 0; i < ARRAY_SIZE(targets); i++) {
        g_autofree char *bundle = NULL;
        g_autofree char *scene = NULL;
        QTestState *q = paired_start(&bundle, &scene, true, false);

        qtest_writel(q, PAIRED + 0x3a4, targets[i]);
        qtest_writel(q, PAIRED + 0x10, 1);
        qtest_writel(q, PAIRED + 8, 1);
        work_input(q);
        work_rise(q);
        qtest_clock_step(q, 2000000);
        g_assert_cmpuint(property_at(q, WORK_PATH, "completed"), ==, 1);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 0);
        paired_untouched(q);
        qtest_quit(q);
        unlink(scene);
        unlink(bundle);
    }
}

static void test_paired_lifecycle(void)
{
    for (unsigned operation = 0; operation < 3; operation++) {
        g_autofree char *bundle = NULL;
        g_autofree char *scene = NULL;
        QTestState *q = paired_start(&bundle, &scene, true, true);

        work_input(q);
        work_rise(q);
        qtest_clock_step(q, 1000000);
        g_assert_cmpuint(property_at(q, WORK_PATH, "completed"), ==, 1);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 0);
        if (operation == 0) {
            qtest_system_reset(q);
        } else if (operation == 1) {
            qtest_writel(q, PAIRED + 0x98, 0x401);
        } else {
            /* Equal retained configuration and table writes do not cancel. */
            qtest_writel(q, PAIRED + 0x98, 0x400);
            qtest_writel(q, PAIRED + 0xb00, 1024);
            qtest_writel(q, PAIRED + 0x1400, 0);
        }
        qtest_clock_step(q, 2000000);
        if (operation < 2) {
            g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 0);
            g_assert_cmpuint(property_at(q, PAIRED_PATH, "cancelled"), ==, 1);
            paired_untouched(q);
        } else {
            g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 1);
            g_assert_cmpuint(property_at(q, PAIRED_PATH, "cancelled"), ==, 0);
            paired_pixels(q, PAIRED_MAIN, 0);
            paired_pixels(q, PAIRED_WORK, 0);
            paired_pixels(q, PAIRED_GRID, 0);
        }
        qtest_quit(q);
        unlink(scene);
        unlink(bundle);
    }
}

static void test_paired_coverage_and_raw_failure(void)
{
    for (unsigned operation = 0; operation < 4; operation++) {
        g_autofree char *bundle = NULL;
        g_autofree char *scene = NULL;
        QTestState *q = paired_start(&bundle, &scene,
                                     operation == 3 ? 2 : operation != 0, true);

        if (operation == 1) {
            /* RAW producer must validate all four actual DMA targets. */
            qtest_writel(q, 0x20069a5c, 0x10020000);
            qtest_writel(q, CAPTURE + 0x104, 0x80);
        } else if (operation == 2) {
            /* Complete coverage still requires matching calibration values. */
            qtest_writel(q, PAIRED + 0x1400, 1);
        }
        work_input(q);
        work_rise(q);
        qtest_clock_step(q, 2000000);
        g_assert_cmpuint(property_at(q, WORK_PATH, "completed"), ==,
                         operation == 1 ? 0 : 1);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==,
                         operation == 3 ? 1 : 0);
        if (operation == 3) {
            /* A wholly unpublished expected-zero table is a NULL table. */
            paired_pixels(q, PAIRED_MAIN, 0);
            paired_pixels(q, PAIRED_WORK, 0);
            paired_pixels(q, PAIRED_GRID, 0);
            g_assert_cmphex(qtest_readl(q, PAIRED + 0x24), ==, 0x1002);
        } else {
            paired_untouched(q);
        }
        qtest_quit(q);
        unlink(scene);
        unlink(bundle);
    }
}

static void test_paired_preserve_results(void)
{
    g_autofree char *bundle = NULL;
    g_autofree char *scene = NULL;
    QTestState *q = paired_start(&bundle, &scene, true, true);

    work_input(q);
    work_rise(q);
    qtest_clock_step(q, 2000000);
    g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 1);
    qtest_writel(q, PAIRED + 0x24, UINT32_MAX);
    work_ack(q);
    work_input(q);
    work_rise(q);
    qtest_clock_step(q, 1000000);
    qtest_writel(q, PAIRED + 0x98, 0x401);
    qtest_clock_step(q, 2000000);
    g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 1);
    g_assert_cmpuint(property_at(q, PAIRED_PATH, "cancelled"), ==, 1);
    paired_pixels(q, PAIRED_MAIN, 0);
    paired_pixels(q, PAIRED_WORK, 0);
    paired_pixels(q, PAIRED_GRID, 0);
    g_assert_cmphex(qtest_readl(q, PAIRED_MAIN + 0x2000), ==, 0xa5a5a5a5);
    g_assert_cmphex(qtest_readl(q, PAIRED_WORK + 0x2000), ==, 0xa5a5a5a5);
    g_assert_cmphex(qtest_readl(q, PAIRED_GRID + 0x1000), ==, 0xa5a5a5a5);
    g_assert_cmphex(qtest_readl(q, PAIRED + 0x24), ==, 0);
    qtest_quit(q);
    unlink(scene);
    unlink(bundle);
}

static void test_paired_backpressure(void)
{
    g_autofree char *bundle = NULL;
    g_autofree char *scene = NULL;
    QTestState *q = paired_start(&bundle, &scene, true, true);

    work_input(q);
    work_rise(q);
    qtest_clock_step(q, 2000000);
    g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 1);
    work_ack(q); /* Leave the paired result pending while RAW continues. */
    work_input(q);
    work_rise(q);
    qtest_clock_step(q, 2000000);
    g_assert_cmpuint(property_at(q, WORK_PATH, "completed"), ==, 2);
    g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 1);
    g_assert_cmphex(qtest_readl(q, PAIRED + 0x24), ==, 0x1002);
    paired_pixels(q, PAIRED_MAIN, 0);
    paired_pixels(q, PAIRED_WORK, 0);
    paired_pixels(q, PAIRED_GRID, 0);
    g_assert_cmphex(qtest_readl(q, PAIRED_MAIN + 0x2000), ==, 0xa5a5a5a5);
    g_assert_cmphex(qtest_readl(q, PAIRED_WORK + 0x2000), ==, 0xa5a5a5a5);
    g_assert_cmphex(qtest_readl(q, PAIRED_GRID + 0x1000), ==, 0xa5a5a5a5);
    qtest_writel(q, PAIRED + 0x24, UINT32_MAX);
    work_ack(q);
    /* A later real RAW publication also permits an earlier queued phase. */
    work_input(q);
    work_rise(q);
    qtest_clock_step(q, 2000000);
    g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 2);
    paired_pixels(q, PAIRED_MAIN, 1);
    paired_pixels(q, PAIRED_WORK, 1);
    paired_pixels(q, PAIRED_GRID, 1);
    qtest_quit(q);
    unlink(scene);
    unlink(bundle);
}

static void test_paired_c_matrix(void)
{
    static const int c[9] = { 512, 0, 0, 0, 1024, 0, 0, 0, 1024 };
    g_autofree char *bundle = NULL;
    g_autofree char *scene = NULL;
    QTestState *q = paired_start(&bundle, &scene, true, false);
    uint8_t expected[0xce0], actual[sizeof(expected)];

    paired_matrix(q, PAIRED, 0xb94, c, 13, true);
    qtest_writel(q, PAIRED + 0x10, 1);
    qtest_writel(q, PAIRED + 8, 1);
    work_input(q);
    work_rise(q);
    qtest_clock_step(q, 2000000);
    /* Fixed arithmetic oracle: C halves red before B; work/grid precede C. */
    memset(expected, 0xa5, sizeof(expected));
    for (unsigned y = 0; y < 28; y++) {
        memset(expected + 16 + 64 * y, 64, 56);
    }
    for (unsigned y = 0; y < 14; y++) {
        for (unsigned x = 0; x < 56; x += 2) {
            expected[16 + 64 * (28 + y) + x] = 144;
            expected[16 + 64 * (28 + y) + x + 1] = 128;
        }
    }
    qtest_memread(q, PAIRED_MAIN - 16, actual, sizeof(actual));
    g_assert_cmpmem(actual, sizeof(actual), expected, sizeof(expected));
    paired_pixels(q, PAIRED_WORK, 0);
    paired_pixels(q, PAIRED_GRID, 0);
    g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 1);
    qtest_quit(q);
    unlink(scene);
    unlink(bundle);
}

static void test_paired_matrix_encoding(void)
{
    static const struct {
        unsigned offset;
        uint32_t value;
    } invalid[] = {
        { 0x304, 1 },          /* Opaque low word must match the profile. */
        { 0x84, 0 },           /* Cold disabled operator is not work. */
        { 0x84, 2 },           /* Unknown operator control cannot be ignored. */
        { 0xf0, 0xfc400 },     /* Stop control is not the normal profile. */
        { 0x9cc, 0x02010101 }, /* Global gain control is a boolean byte. */
        { 0x9cc, 0x01020101 }, /* Channel controls are boolean bytes. */
        { 0x9d4, 0x00010040 }, /* Blue gain has no upper-half field. */
        { 0xb00, 0x80000400 }, /* A0 opaque control is not implemented. */
        { 0xb00, 0x00002400 }, /* A0 bit13 is outside its coefficient. */
        { 0xb04, 0x20000000 }, /* Reserved bit above a packed A coefficient. */
        { 0xb64, 0xffff0200 }, /* Positive B0 has invalid extension bits. */
        { 0xb64, 0x00000f00 }, /* Negative B0 requires whole-word extension. */
        { 0xb94, 0xffff0400 }, /* Positive C0 has invalid upper bits. */
        { 0xb94, 0x00001f00 }, /* Negative C0 is not sign-extended. */
        { 0xdf4, 0x01000000 }, /* Noise triplets have no fourth byte. */
        { 0xdf4, 0x00020000 }, /* Channel enable is a boolean byte. */
        { 0xdf8, 0x80000001 }, /* Reducible feedback. */
        { 0xe04, 0x00100000 }, /* A shifrt above15 is unsuppoted. */
        { 0xe10, 0x040003fc }, /* A noise limit exceeds ten bits. */
        { 0x218, 0 },         /* The primary front/back ring must agree. */
        { 0x228, PAIRED_MAIN }, /* Partial or unrelated second rings reject. */
        { 0xfc, 1 },          /* Secondary selection needs every mirror. */
        { 0xfc, 2 },          /* The selector is boolean. */
        { 0x3a8, PAIRED_GRID }, /* A grid mirror needs all four slots. */
        { 0x4a8, PAIRED_MAIN }, /* Main Y alone is not a complete Y/UV group. */
        { 0x4b8, PAIRED_MAIN + 64 * 28 }, /* UV alone is also incomplete. */
    };

    for (unsigned i = 0; i < ARRAY_SIZE(invalid); i++) {
        g_autofree char *bundle = NULL;
        g_autofree char *scene = NULL;
        QTestState *q = paired_start(&bundle, &scene, true, false);

        if (invalid[i].offset == 0x228 || invalid[i].offset == 0x3a8 ||
            invalid[i].offset == 0x4a8 || invalid[i].offset == 0x4b8 ||
            (invalid[i].offset == 0xfc && invalid[i].value == 1)) {
            paired_secondary(q, 0);
            if (invalid[i].offset == 0x3a8) {
                for (unsigned p = 0; p < 4; p++) {
                    qtest_writel(q, PAIRED + 0x3a8 + 4 * p, 0);
                }
            }
            if (invalid[i].offset == 0x4a8 || invalid[i].offset == 0x4b8 ||
                invalid[i].offset == 0xfc) {
                for (unsigned p = 0; p < 8; p++) {
                    qtest_writel(q, PAIRED + 0x4a8 + 4 * p, 0);
                }
            }
            qtest_writel(q, PAIRED + 0xfc, 1);
        }
        qtest_writel(q, PAIRED + invalid[i].offset, invalid[i].value);
        qtest_writel(q, PAIRED + 0x10, 1);
        qtest_writel(q, PAIRED + 8, 1);
        work_input(q);
        work_rise(q);
        qtest_clock_step(q, 2000000);
        g_assert_cmpuint(property_at(q, WORK_PATH, "completed"), ==, 1);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 0);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "rejected"), >, 0);
        paired_untouched(q);
        qtest_quit(q);
        unlink(scene);
        unlink(bundle);
    }
}

static void test_paired_source_history(void)
{
    static const uint8_t gray[] = {
        'P', '5', '\n', '1', ' ', '1', '\n', '2', '5', '5', '\n', 129,
    };

    for (unsigned mode = 0; mode < 5; mode++) {
        bool invalidate = mode == 1;
        g_autofree char *bundle = NULL;
        g_autofree char *scene = NULL;
        QTestState *q = paired_start_profile(&bundle, &scene, true, true,
                                             mode == 4);
        uint8_t expected[0xce0], actual[sizeof(expected)];
        static const uint8_t grid[][3] = {
            { 0x14, 0x48, 0x81 }, { 0x18, 0x88, 0x81 },
        };

        if (mode == 4) {
            qtest_writel(q, PAIRED + 0x700, 0x101);
            qtest_writel(q, PAIRED + 0x708, 0x02000200);
            qtest_writel(q, PAIRED + 0x70c, 0x02000200);
            qtest_writel(q, PAIRED + 0x720, 32767);
        }

        /* Two successful publications establish both aliased history units. */
        for (unsigned phase = 0; phase < 2; phase++) {
            work_input(q);
            work_rise(q);
            qtest_clock_step(q, 2000000);
            g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==,
                             phase + 1);
            paired_pixels(q, PAIRED_WORK, phase);
            qtest_writel(q, PAIRED + 0x24, UINT32_MAX);
            work_ack(q);
        }
        if (invalidate) {
            /* Source commits withdraw RAW without changing paired config. */
            qtest_writel(q, CAPTURE + 0x500, 1);
            qtest_writel(q, CAPTURE + 0x100, 1);
            qtest_writel(q, CAPTURE + 0x500, 0);
            qtest_writel(q, CAPTURE + 0x100, 1);
        } else if (mode == 2) {
            /* Noise-only configuration must not discard pre-color history. */
            qtest_writel(q, PAIRED + 0xdf8, 0xf8040000);
        } else if (mode == 4) {
            qtest_writel(q, PAIRED + 0x70c, 0x02000201);
        } else if (mode == 3) {
            for (unsigned i = 0; i < 4; i++) {
                uint32_t address = qtest_readl(q, PAIRED + 0x310 + 4 * i);

                qtest_writel(q, PAIRED + 0x228 + 4 * i, address);
                qtest_writel(q, PAIRED + 0x320 + 4 * i, address);
                qtest_writel(q, PAIRED + 0x3a8 + 4 * i,
                             qtest_readl(q, PAIRED + 0x398 + 4 * i));
                qtest_writel(q, PAIRED + 0x4a8 + 4 * i,
                             qtest_readl(q, PAIRED + 0x488 + 4 * i));
                qtest_writel(q, PAIRED + 0x4b8 + 4 * i,
                             qtest_readl(q, PAIRED + 0x498 + 4 * i));
            }
        }
        g_assert_true(g_file_set_contents(scene, (const char *)gray,
                                          sizeof(gray), NULL));
        qtest_qmp_assert_success(q,
            "{'execute':'qom-set','arguments':{'path':'/machine/cmos',"
            "'property':'image','value':%s}}", scene);
        work_input(q);
        work_rise(q);
        qtest_clock_step(q, 2000000);
        g_assert_cmphex(qtest_readl(q, WORK_BUFFER + 0x3000), ==, 0x18818818);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 3);

        /*
         * Fixed discriminator: old history blends2072 to2068/residual16;
         * a new source epoch produces fresh2072/residual0 despite old RAM.
         */
        memset(expected, 0xa5, sizeof(expected));
        for (unsigned y = 0; y < 18; y++) {
            for (unsigned x = 0; x < 32; x++) {
                stw_le_p(expected + 16 + 80 * y + 2 * x,
                          invalidate ? 2072 : 2068);
                stw_le_p(expected + 16 + 80 * (18 + y) + 2 * x,
                          invalidate ? 0 : 16);
            }
        }
        qtest_memread(q, PAIRED_WORK - 16, actual, sizeof(actual));
        g_assert_cmpmem(actual, sizeof(actual), expected, sizeof(expected));
        memset(expected, 0xa5, sizeof(expected));
        for (unsigned y = 0; y < 34; y++) {
            for (unsigned x = 0; x < 90; x += 3) {
                memcpy(expected + 16 + 96 * y + x, grid[invalidate], 3);
            }
        }
        qtest_memread(q, PAIRED_GRID + 0x2000 - 16, actual, sizeof(actual));
        g_assert_cmpmem(actual, sizeof(actual), expected, sizeof(expected));
        g_assert_cmphex(qtest_readl(q, PAIRED + 0x24), ==, 0x1002);
        qtest_quit(q);
        unlink(scene);
        unlink(bundle);
    }
}

static void paired_main_at(QTestState *q, uint32_t address, uint8_t luma,
                           uint8_t u, uint8_t v)
{
    uint8_t expected[0xce0], actual[sizeof(expected)];

    memset(expected, 0xa5, sizeof(expected));
    for (unsigned y = 0; y < 28; y++) {
        memset(expected + 16 + 64 * y, luma, 56);
    }
    for (unsigned y = 0; y < 14; y++) {
        for (unsigned x = 0; x < 56; x += 2) {
            expected[16 + 64 * (28 + y) + x] = u;
            expected[16 + 64 * (28 + y) + x + 1] = v;
        }
    }
    qtest_memread(q, address - 16, actual, sizeof(actual));
    g_assert_cmpmem(actual, sizeof(actual), expected, sizeof(expected));
}

static void paired_main_oracle(QTestState *q, uint8_t luma,
                               uint8_t u, uint8_t v)
{
    paired_main_at(q, PAIRED_MAIN, luma, u, v);
}

static void test_paired_gain(void)
{
    static const struct {
        uint32_t control;
        uint8_t y, u, v;
    } cases[] = {
        { 0x01010101, 193, 140, 48 }, /* R1.5/G0.5/B0.25, before A. */
        { 0x01010000, 193, 144, 64 }, /* Only the R boolean is enabled. */
        { 0x00010101, 129, 144, 96 }, /* Global off ignores all three gains. */
    };

    for (unsigned mode = 0; mode < ARRAY_SIZE(cases); mode++) {
        g_autofree char *bundle = NULL;
        g_autofree char *scene = NULL;
        QTestState *q = paired_start(&bundle, &scene, true, false);

        qtest_writel(q, PAIRED + 0x9cc, cases[mode].control);
        qtest_writel(q, PAIRED + 0x9d0, (384 << 16) | 128);
        qtest_writel(q, PAIRED + 0x9d4, 64);
        qtest_writel(q, PAIRED + 0x10, 1);
        qtest_writel(q, PAIRED + 8, 1);
        work_input(q);
        work_rise(q);
        qtest_clock_step(q, 2000000);
        g_assert_cmphex(qtest_readl(q, WORK_BUFFER + 0x1000), ==, 0x08808808);
        paired_main_oracle(q, cases[mode].y, cases[mode].u, cases[mode].v);
        paired_pixels(q, PAIRED_WORK, 0);
        paired_pixels(q, PAIRED_GRID, 0);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 1);
        g_assert_cmphex(qtest_readl(q, PAIRED + 0x24), ==, 0x1002);
        qtest_writel(q, PAIRED + 0xe8, 0);
        g_assert_true(qtest_get_irq(q, 99 - 32));
        qtest_writel(q, PAIRED + 0x24, UINT32_MAX);
        g_assert_false(qtest_get_irq(q, 99 - 32));
        qtest_quit(q);
        unlink(scene);
        unlink(bundle);
    }
}

static void test_paired_quad(void)
{
    g_autofree char *bundle = NULL;
    g_autofree char *scene = NULL;
    QTestState *q = paired_start_profile(&bundle, &scene, true, false, true);

    qtest_writel(q, PAIRED + 0x700, 0x101);
    qtest_writel(q, PAIRED + 0x708, (768 << 16) | 512);
    qtest_writel(q, PAIRED + 0x70c, (512 << 16) | 256);
    qtest_writel(q, PAIRED + 0x710, (0x1ff0 << 16) | 16);
    qtest_writel(q, PAIRED + 0x714, (0x1ff0 << 16) | 16);
    qtest_writel(q, PAIRED + 0x718, (16 << 16) | 32);
    qtest_writel(q, PAIRED + 0x71c, (64 << 16) | 128);
    qtest_writel(q, PAIRED + 0x720, 4095);
    qtest_writel(q, PAIRED + 0x10, 1);
    qtest_writel(q, PAIRED + 8, 1);
    work_input(q);
    work_rise(q);
    qtest_clock_step(q, 2000000);
    g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 1);
    /* RAW=2056; RGB=(3076,2104,1164), then the fixture A/B matrices. */
    paired_main_oracle(q, 192, 152, 65);
    paired_pixels(q, PAIRED_WORK, 0);
    paired_pixels(q, PAIRED_GRID, 0);
    g_assert_cmphex(qtest_readl(q, PAIRED + 0x24), ==, 0x1002);
    qtest_quit(q);
    unlink(scene);
    unlink(bundle);
}

static void test_paired_quad_boundaries(void)
{
    static const struct { unsigned offset; uint32_t value; } invalid[] = {
        { 0x700, 0x100 }, { 0x704, 1 },
        { 0x708, 0x40000200 }, { 0x70c, 0x02004000 },
        { 0x710, 0x20000000 }, { 0x714, 0x2000 },
        { 0x720, 0x8000 }, { 0x720, 0x10000 },
    };

    for (unsigned i = 0; i < ARRAY_SIZE(invalid) + 3; i++) {
        g_autofree char *bundle = NULL;
        g_autofree char *scene = NULL;
        QTestState *q = paired_start_profile(&bundle, &scene, true, false,
                                             true);

        qtest_writel(q, PAIRED + 0x700, 0x101);
        qtest_writel(q, PAIRED + 0x708, 0x02000200);
        qtest_writel(q, PAIRED + 0x70c, 0x02000200);
        qtest_writel(q, PAIRED + 0x720, 32767);
        if (i < ARRAY_SIZE(invalid)) {
            qtest_writel(q, PAIRED + invalid[i].offset, invalid[i].value);
        }
        qtest_writel(q, PAIRED + 0x10, 1);
        qtest_writel(q, PAIRED + 8, 1);
        work_input(q);
        work_rise(q);
        qtest_clock_step(q, 1000000);
        if (i == ARRAY_SIZE(invalid)) {
            qtest_system_reset(q);
        } else if (i > ARRAY_SIZE(invalid)) {
            qtest_writel(q, PAIRED + 0x70c,
                         i == ARRAY_SIZE(invalid) + 1 ?
                         0x02000201 : 0x02000200);
        }
        qtest_clock_step(q, 2000000);
        if (i == ARRAY_SIZE(invalid) + 2) {
            g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 1);
            paired_pixels(q, PAIRED_MAIN, 0);
        } else {
            g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 0);
            paired_untouched(q);
        }
        if (i < ARRAY_SIZE(invalid)) {
            g_assert_cmpuint(property_at(q, PAIRED_PATH, "rejected"), >, 0);
        } else {
            g_assert_cmpuint(property_at(q, PAIRED_PATH, "cancelled"), ==,
                             i == ARRAY_SIZE(invalid) + 2 ? 0 : 1);
        }
        qtest_quit(q);
        unlink(scene);
        unlink(bundle);
    }
}

static void paired_curve(QTestState *q, unsigned selector, uint16_t gain,
                         unsigned words, bool bad_padding)
{
    uint32_t base = PAIRED + 0x2890 + selector * 516;

    for (unsigned i = 0; i < words; i++) {
        uint32_t value = gain;

        if (i < 128) {
            value |= (uint32_t)gain << 16;
        } else if (bad_padding) {
            value |= 1u << 16;
        }
        qtest_writel(q, base + 4 * i, value);
    }
}

static void test_paired_curves(void)
{
    static const struct {
        uint16_t first, second;
        uint8_t y, u, v;
    } cases[] = {
        { 512, 0, 64, 136, 112 },     /* One Q10 half-gain curve. */
        { 1024, 0, 129, 144, 96 },    /* Published unity is natural identity. */
        { 512, 256, 16, 130, 124 },   /* Distinct serial half/quarter gains. */
    };

    for (unsigned mode = 0; mode < ARRAY_SIZE(cases); mode++) {
        g_autofree char *bundle = NULL;
        g_autofree char *scene = NULL;
        QTestState *q = paired_start(&bundle, &scene, true, false);

        paired_curve(q, 0, cases[mode].first, 129, false);
        if (cases[mode].second) {
            paired_curve(q, 1, cases[mode].second, 129, false);
        }
        qtest_writel(q, PAIRED + 0x10, 1);
        qtest_writel(q, PAIRED + 8, 1);
        work_input(q);
        work_rise(q);
        qtest_clock_step(q, 2000000);
        g_assert_cmphex(qtest_readl(q, WORK_BUFFER + 0x1000), ==, 0x08808808);
        paired_main_oracle(q, cases[mode].y, cases[mode].u, cases[mode].v);
        /* Curves affect color only; history and grid keep pre-color samples. */
        paired_pixels(q, PAIRED_WORK, 0);
        paired_pixels(q, PAIRED_GRID, 0);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 1);
        g_assert_cmphex(qtest_readl(q, PAIRED + 0x24), ==, 0x1002);
        qtest_writel(q, PAIRED + 0xe8, 0);
        g_assert_true(qtest_get_irq(q, 99 - 32));
        qtest_writel(q, PAIRED + 0x24, UINT32_MAX);
        g_assert_false(qtest_get_irq(q, 99 - 32));
        qtest_quit(q);
        unlink(scene);
        unlink(bundle);
    }
}

static void test_paired_curve_encoding(void)
{
    for (unsigned mode = 0; mode < 4; mode++) {
        g_autofree char *bundle = NULL;
        g_autofree char *scene = NULL;
        QTestState *q = paired_start(&bundle, &scene, true, false);

        /* Both selectors reject partial coverage and nonzero upper padding. */
        paired_curve(q, mode / 2, 512, mode % 2 ? 129 : 128, mode % 2);
        qtest_writel(q, PAIRED + 0x10, 1);
        qtest_writel(q, PAIRED + 8, 1);
        work_input(q);
        work_rise(q);
        qtest_clock_step(q, 2000000);
        g_assert_cmpuint(property_at(q, WORK_PATH, "completed"), ==, 1);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 0);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "rejected"), >, 0);
        paired_untouched(q);
        qtest_quit(q);
        unlink(scene);
        unlink(bundle);
    }
}

static void test_paired_coverage_cancellation(void)
{
    for (unsigned table = 0; table < 2; table++) {
        g_autofree char *bundle = NULL;
        g_autofree char *scene = NULL;
        QTestState *q = paired_start(&bundle, &scene, table ? 2 : 1, true);

        work_input(q);
        work_rise(q);
        qtest_clock_step(q, 1000000);
        g_assert_cmpuint(property_at(q, WORK_PATH, "completed"), ==, 1);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 0);
        if (table) {
            /* First publication changes coverage despite equal zero values. */
            qtest_writel(q, PAIRED + 0x1400, 0);
        } else {
            /* A published zero curve differs from an absent curve. */
            paired_curve(q, 0, 0, 129, false);
        }
        qtest_clock_step(q, 2000000);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "cancelled"), ==, 1);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 0);
        paired_untouched(q);
        if (table) {
            /* A partial table must not resume the admitted RAW. */
            qtest_writel(q, PAIRED + 0x10, 1);
            qtest_clock_step(q, 2000000);
            g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 0);
            paired_untouched(q);
            for (unsigned offset = 0x1404; offset < 0x1440; offset += 4) {
                qtest_writel(q, PAIRED + offset, 0);
            }
        }
        /* The cancelled job never consumed its actual RAW generation. */
        qtest_writel(q, PAIRED + 0x10, 1);
        qtest_clock_step(q, 2000000);
        g_assert_cmpuint(property_at(q, WORK_PATH, "completed"), ==, 1);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 1);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "cancelled"), ==, 1);
        paired_main_oracle(q, table ? 129 : 0,
                            table ? 144 : 128, table ? 96 : 128);
        paired_pixels(q, PAIRED_WORK, 0);
        paired_pixels(q, PAIRED_GRID, 0);
        g_assert_cmphex(qtest_readl(q, PAIRED + 0x24), ==, 0x1002);
        qtest_quit(q);
        unlink(scene);
        unlink(bundle);
    }
}

static void paired_noise(QTestState *q, uint32_t feedback, unsigned channel)
{
    static const int identity[9] = { 1024, 0, 0, 0, 1024, 0, 0, 0, 1024 };
    int y_from_channel[9] = { 0 };
    unsigned shift = 16 - 8 * channel;

    y_from_channel[channel] = 512;
    paired_matrix(q, PAIRED, 0xb00, identity, 13, false);
    paired_matrix(q, PAIRED, 0xb64, y_from_channel, 12, false);
    qtest_writel(q, PAIRED + 0xdf4, 1U << shift);
    for (unsigned i = 0; i < 3; i++) {
        qtest_writel(q, PAIRED + 0xdf8 + 4 * i,
                     i == channel ? feedback : 0);
    }
    qtest_writel(q, PAIRED + 0xe0c, 0xffU << shift);
    qtest_writel(q, PAIRED + 0xe10, 0x03fc03fc);
}

static void paired_noise_oracle(QTestState *q, unsigned phase, bool outside)
{
    static const uint8_t first_rows[][2][4] = {
        { { 143, 128, 115, 114 }, { 120, 116, 113, 113 } },
        { { 129, 137, 140, 126 }, { 125, 135, 135, 140 } },
    };
    uint8_t pixels[64 * 42];

    qtest_memread(q, PAIRED_MAIN + (phase % 2) * 0x2000, pixels,
                  sizeof(pixels));
    for (unsigned y = 0; y < 2; y++) {
        g_assert_cmpmem(pixels + 64 * y, 4, first_rows[outside][y], 4);
    }
    for (unsigned y = 0; y < 42; y++) {
        for (unsigned x = 0; x < 64; x++) {
            if (x >= 56) {
                g_assert_cmphex(pixels[64 * y + x], ==, 0xa5);
            } else if (y >= 28) {
                g_assert_cmpuint(pixels[64 * y + x], ==, 128);
            } else {
                g_assert_cmpuint(pixels[64 * y + x], >=, 112);
                g_assert_cmpuint(pixels[64 * y + x], <=, 145);
            }
        }
    }
    g_assert_cmphex(qtest_readl(q, PAIRED_MAIN +
                     (phase % 2) * 0x2000 - 4), ==, 0xa5a5a5a5);
    paired_pixels(q, PAIRED_WORK, phase % 2);
    paired_pixels(q, PAIRED_GRID, phase % 4);
}

static void test_paired_noise(void)
{
    static const uint32_t masks[] = { 0xf8200000, 0xf8040000, 0x80200003 };
    g_autofree char *bundle = NULL;
    g_autofree char *scene = NULL;
    QTestState *q = paired_start(&bundle, &scene, true, false);

    paired_noise(q, masks[0], 0);
    for (unsigned i = 0; i < 4; i++) {
        uint32_t address = qtest_readl(q, PAIRED + 0x310 + 4 * i);

        qtest_writel(q, PAIRED + 0x228 + 4 * i, address);
        qtest_writel(q, PAIRED + 0x320 + 4 * i, address);
        qtest_writel(q, PAIRED + 0x3a8 + 4 * i,
                     qtest_readl(q, PAIRED + 0x398 + 4 * i));
        qtest_writel(q, PAIRED + 0x4a8 + 4 * i,
                     qtest_readl(q, PAIRED + 0x488 + 4 * i));
        qtest_writel(q, PAIRED + 0x4b8 + 4 * i,
                     qtest_readl(q, PAIRED + 0x498 + 4 * i));
    }
    qtest_writel(q, PAIRED + 0x10, 1);
    qtest_writel(q, PAIRED + 8, 1);
    for (unsigned batch = 0; batch < ARRAY_SIZE(masks); batch++) {
        if (batch) {
            paired_noise(q, masks[batch], batch);
            if (batch == 1) {
                /* An inactive ring's partial publication cannot block FC1. */
                qtest_writel(q, PAIRED + 0x310,
                             qtest_readl(q, PAIRED + 0x310));
                qtest_writel(q, PAIRED + 0x320,
                             qtest_readl(q, PAIRED + 0x320));
            } else {
                for (unsigned i = 1; i < 4; i++) {
                    qtest_writel(q, PAIRED + 0x310 + 4 * i,
                                 qtest_readl(q, PAIRED + 0x310 + 4 * i));
                }
            }
            qtest_writel(q, PAIRED + 0xfc, batch == 1);
            qtest_writel(q, PAIRED + 0x10, 1);
        }
        for (unsigned phase = 0; phase < 4; phase++) {
            work_input(q);
            work_rise(q);
            qtest_clock_step(q, 2000000);
            if (batch == 1 && phase == 0) {
                /* The selected ring still needs all four publications. */
                g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"),
                                 ==, 4);
                g_assert_cmphex(qtest_readl(q, PAIRED + 0x24), ==, 0);
                for (unsigned i = 1; i < 4; i++) {
                    qtest_writel(q, PAIRED + 0x320 + 4 * i,
                                 qtest_readl(q, PAIRED + 0x320 + 4 * i));
                }
                qtest_writel(q, PAIRED + 0x10, 1);
                qtest_clock_step(q, 2000000);
            }
            g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==,
                             batch * 4 + phase + 1);
            paired_noise_oracle(q, phase, batch == 2);
            g_assert_cmphex(qtest_readl(q, PAIRED + 0x24), ==,
                             0x1002 | (phase == 3 ? 0x400 : 0));
            qtest_writel(q, PAIRED + 0x24, UINT32_MAX);
            work_ack(q);
        }
    }
    g_assert_cmpuint(property_at(q, PAIRED_PATH, "rejected"), ==, 0);
    g_assert_cmphex(qtest_readl(q, PAIRED + 0x118), ==, 12 * 56 * 28);
    qtest_quit(q);
    unlink(scene);
    unlink(bundle);
}

static void test_paired_noise_cancel(void)
{
    g_autofree char *bundle = NULL;
    g_autofree char *scene = NULL;
    QTestState *q = paired_start(&bundle, &scene, true, false);

    paired_noise(q, 0xf8200000, 0);
    qtest_writel(q, PAIRED + 0x10, 1);
    qtest_writel(q, PAIRED + 8, 1);
    work_input(q);
    work_rise(q);
    qtest_clock_step(q, 1000000);
    qtest_writel(q, PAIRED + 0xdf8, 0x80200003);
    qtest_clock_step(q, 2000000);
    g_assert_cmpuint(property_at(q, PAIRED_PATH, "cancelled"), ==, 1);
    g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 0);
    paired_untouched(q);
    qtest_writel(q, PAIRED + 0x10, 1);
    qtest_clock_step(q, 2000000);
    g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 1);
    paired_noise_oracle(q, 0, true);
    qtest_qmp_assert_success(q, "{'execute':'system_reset'}");
    qtest_clock_step(q, 2000000);
    g_assert_cmphex(qtest_readl(q, PAIRED + 0xdf8), ==, 0);
    g_assert_cmphex(qtest_readl(q, PAIRED + 0xe10), ==, 0);
    g_assert_cmphex(qtest_readl(q, PAIRED + 0x24), ==, 0);
    g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 1);
    g_assert_false(qtest_get_irq(q, 99 - 32));
    qtest_quit(q);
    unlink(scene);
    unlink(bundle);
}

static void paired_filter(QTestState *q, unsigned control, unsigned clip,
                          unsigned retain, unsigned base, unsigned slope)
{
    const unsigned fields[] = { retain, base, 1, slope, 0, 16 };

    qtest_writel(q, PAIRED + 0x594, control);
    for (unsigned pair = 0; pair < ARRAY_SIZE(fields); pair++) {
        for (unsigned channel = 0; channel < 3; channel++) {
            qtest_writel(q, PAIRED + 0x598 + 12 * pair + 4 * channel,
                         (fields[pair] << 16) | fields[pair]);
        }
    }
    qtest_writel(q, PAIRED + 0x608, clip);
}

static void paired_photo(QTestState *q, uint32_t base)
{
    /* Chunk metadata is deliberately different from the actual 36-row ROI. */
    static const struct { unsigned offset, value; } packet[] = {
        { 0x88, 2 }, { 0x8c, 0 }, { 0x94, 92 }, { 0x9c, 879 },
        { 0xa8, 2 }, { 0xe0, 0x156 }, { 0x10c, 1 }, { 0x124, 0 },
        { 0x138, 1 }, { 0x13c, 22 << 16 }, { 0x140, 40 << 16 },
        { 0x3d8, 0 }, { 0x408, 0 }, { 0x410, 0 }, { 0x424, 2 },
    };

    for (unsigned i = 0; i < ARRAY_SIZE(packet); i++) {
        qtest_writel(q, base + packet[i].offset, packet[i].value);
    }
    /* Disabled WORK pointers must neither be validated nor accessed. */
    for (unsigned offset = 0x430; offset <= 0x470; offset += 4) {
        qtest_writel(q, base + offset, 0);
    }
}

static void test_paired_photo(void)
{
    for (unsigned mode = 0; mode < 6; mode++) {
        g_autofree char *bundle = NULL;
        g_autofree char *scene = NULL;
        QTestState *q = paired_start(&bundle, &scene, true, false);
        uint8_t unchanged[0xce0], actual[sizeof(unchanged)];
        bool success = mode == 0 || mode >= 4;

        paired_photo(q, PAIRED);
        if (mode == 4) {
            qtest_writel(q, PAIRED + 0x430, 1);
            qtest_writel(q, PAIRED + 0x470, 3);
        }
        if (mode == 1) {
            qtest_writel(q, PAIRED + 0x9c, 35); /* RAW height is not count. */
        } else if (mode == 2) {
            qtest_writel(q, PAIRED + 0x13c, (22 << 16) | 1);
        } else if (mode == 3) {
            /* A shared stream is not a contiguous RAM surface. */
            qtest_writel(q, PAIRED + 0x424, 3);
            for (unsigned phase = 0; phase < 4; phase++) {
                qtest_writel(q, PAIRED + 0x488 + phase * 4, 0x40000000);
                qtest_writel(q, PAIRED + 0x498 + phase * 4, 0x40008000);
            }
        }
        qtest_writel(q, PAIRED + 0x10, 1);
        qtest_writel(q, PAIRED + 8, 1);
        work_input(q);
        work_rise(q);
        qtest_clock_step(q, 1000000);
        if (mode == 5) {
            qtest_writel(q, PAIRED + 0x430, 1);
            qtest_writel(q, PAIRED + 0x470, 3);
        }
        qtest_clock_step(q, 1000000);
        g_assert_cmpuint(property_at(q, "/machine/capture-processing",
                                       "completed"), ==, 1);
        g_assert_cmpuint(property_at(q, "/machine/paired-processing",
                                       "completed"), ==, success);
        memset(unchanged, 0xa5, sizeof(unchanged));
        for (unsigned phase = 0; phase < 2; phase++) {
            qtest_memread(q, PAIRED_WORK + phase * 0x2000 - 16,
                          actual, sizeof(actual));
            g_assert_cmpmem(actual, sizeof(actual),
                            unchanged, sizeof(unchanged));
        }
        if (success) {
            paired_pixels(q, PAIRED_MAIN, 0);
            paired_pixels(q, PAIRED_GRID, 0);
            g_assert_cmphex(qtest_readl(q, PAIRED + 0x24), ==, 0x1002);
            g_assert_cmpuint(property_at(q, "/machine/paired-processing",
                                           "bytes"), ==, 56 * 42 + 90 * 34);
        } else {
            qtest_memread(q, PAIRED_MAIN - 16, actual, sizeof(actual));
            g_assert_cmpmem(actual, sizeof(actual),
                            unchanged, sizeof(unchanged));
            g_assert_cmphex(qtest_readl(q, PAIRED + 0x24), ==, 0);
        }
        qtest_quit(q);
        unlink(bundle);
        unlink(scene);
    }
}

static void test_paired_filter(void)
{
    static const uint8_t step[] = {
        'P', '5', '\n', '2', ' ', '1', '\n', '2', '5', '5', '\n', 64, 192,
    };
    static const uint8_t expected[][4] = {
        { 68, 100, 157, 189 }, /* One cross-neighbor pass. */
        { 72, 121, 137, 185 }, /* Horizontal then vertical refinement. */
        { 64, 80, 177, 193 },  /* Zero residual: existing spatial only. */
    };

    for (unsigned mode = 0; mode < ARRAY_SIZE(expected); mode++) {
        g_autofree char *bundle = NULL;
        g_autofree char *scene = NULL;
        uint8_t pixels[64 * 28];
        QTestState *q = paired_start(&bundle, &scene, true, false);

        g_assert_true(g_file_set_contents(scene, (const char *)step,
                                          sizeof(step), NULL));
        qtest_qmp_assert_success(q,
            "{'execute':'qom-set','arguments':{'path':'/machine/cmos',"
            "'property':'image','value':%s}}", scene);
        paired_filter(q, mode == 1 ? 6 : 0, mode == 2, 0,
                       mode == 2 ? 0 : 16383, 0);
        qtest_writel(q, PAIRED + 0x10, 1);
        qtest_writel(q, PAIRED + 8, 1);
        work_input(q);
        work_rise(q);
        qtest_clock_step(q, 2000000);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 1);
        qtest_memread(q, PAIRED_MAIN, pixels, sizeof(pixels));
        for (unsigned row = 0; row < 28; row++) {
            for (unsigned x = 0; x < 4; x++) {
                g_assert_cmpuint(pixels[64 * row + 26 + x], ==,
                                 expected[mode][x]);
            }
            for (unsigned x = 56; x < 64; x++) {
                g_assert_cmphex(pixels[64 * row + x], ==, 0xa5);
            }
        }
        g_assert_cmphex(qtest_readl(q, PAIRED_MAIN - 4), ==, 0xa5a5a5a5);
        g_assert_cmphex(qtest_readl(q, PAIRED + 0x24), ==, 0x1002);
        g_assert_cmphex(qtest_readl(q, PAIRED + 0x118), ==, 56 * 28);
        qtest_quit(q);
        unlink(scene);
        unlink(bundle);
    }
}

static void test_paired_filter_boundaries(void)
{
    for (unsigned mode = 0; mode < 10; mode++) {
        g_autofree char *bundle = NULL;
        g_autofree char *scene = NULL;
        QTestState *q = paired_start(&bundle, &scene, true, false);

        if (mode == 0) {
            /* Incomplete zero publication. */
            qtest_writel(q, PAIRED + 0x594, 0);
        } else {
            paired_filter(q, 0, 1, 6087, 50, 67);
            if (mode == 1) {
                qtest_writel(q, PAIRED + 0x594, 2);
            } else if (mode == 2) {
                qtest_writel(q, PAIRED + 0x608, 2);
            } else if (mode >= 3 && mode <= 6) {
                for (unsigned i = 0; i < 3; i++) {
                    qtest_writel(q, PAIRED + 0x598 + 12 * (mode - 3) + 4 * i,
                                 mode == 5 ? 0x100010 : 0x40004000);
                }
            } else if (mode == 7) {
                qtest_writel(q, PAIRED + 0x59c, 0x17c817c8);
            } else if (mode == 8) {
                for (unsigned i = 0; i < 3; i++) {
                    qtest_writel(q, PAIRED + 0x5c8 + 4 * i, 0x10001);
                }
            } else {
                for (unsigned i = 0; i < 3; i++) {
                    qtest_writel(q, PAIRED + 0x5d4 + 4 * i, 0xf000f);
                }
            }
        }
        qtest_writel(q, PAIRED + 0x10, 1);
        qtest_writel(q, PAIRED + 8, 1);
        work_input(q);
        work_rise(q);
        qtest_clock_step(q, 2000000);
        g_assert_cmpuint(property_at(q, WORK_PATH, "completed"), ==, 1);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 0);
        paired_untouched(q);
        qtest_quit(q);
        unlink(scene);
        unlink(bundle);
    }
}

static void test_paired_filter_lifecycle(void)
{
    for (unsigned mode = 0; mode < 3; mode++) {
        g_autofree char *bundle = NULL;
        g_autofree char *scene = NULL;
        QTestState *q = paired_start(&bundle, &scene, true, false);

        paired_filter(q, 0, 1, 6087, 50, 67);
        qtest_writel(q, PAIRED + 0x10, 1);
        qtest_writel(q, PAIRED + 8, 1);
        work_input(q);
        work_rise(q);
        qtest_clock_step(q, 1000000);
        paired_filter(q, 0, 1, mode == 1 ? 6762 : 6087, 50, 67);
        if (mode == 2) {
            qtest_qmp_assert_success(q, "{'execute':'system_reset'}");
        }
        qtest_clock_step(q, 2000000);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==,
                         mode == 0);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "cancelled"), ==,
                         mode != 0);
        if (mode == 0) {
            paired_pixels(q, PAIRED_MAIN, 0);
            paired_pixels(q, PAIRED_WORK, 0);
            paired_pixels(q, PAIRED_GRID, 0);
        } else {
            paired_untouched(q);
        }
        qtest_quit(q);
        unlink(scene);
        unlink(bundle);
    }
}

static void test_paired_stride(void)
{
    for (unsigned mode = 0; mode < 3; mode++) {
        g_autofree char *bundle = NULL;
        g_autofree char *scene = NULL;
        QTestState *q = paired_start(&bundle, &scene, true, false);

        qtest_writel(q, PAIRED + (mode == 0 ? 0x4c8 :
                                  mode == 1 ? 0x470 : 0x394), 0);
        for (unsigned phase = 0; phase < 4 && mode != 2; phase++) {
            if (mode == 0) {
                qtest_writel(q, PAIRED + 0x498 + 4 * phase,
                    qtest_readl(q, PAIRED + 0x488 + 4 * phase));
            } else {
                uint32_t work = qtest_readl(q, PAIRED + 0x430 + 4 * phase);

                qtest_writel(q, PAIRED + 0x440 + 4 * phase, work);
                qtest_writel(q, PAIRED + 0x460 + 4 * phase, work);
            }
        }
        qtest_writel(q, PAIRED + 0x10, 1);
        qtest_writel(q, PAIRED + 8, 1);
        work_input(q);
        work_rise(q);
        qtest_clock_step(q, 2000000);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 0);
        paired_untouched(q);
        qtest_quit(q);
        unlink(bundle);
        unlink(scene);
    }
}

#define PAIRED_GATE 0x20100000

static char *paired_gate_path(QTestState *q)
{
    QDict *reply = qtest_qmp(q, "{'execute':'qom-list',"
                               "'arguments':{'path':'/machine/unattached'}}");
    QListEntry *entry;
    char *path = NULL;

    QLIST_FOREACH_ENTRY(qdict_get_qlist(reply, "return"), entry) {
        const QDict *child = qobject_to(QDict, qlist_entry_obj(entry));

        if (!strcmp(qdict_get_str(child, "type"),
                     "child<nikon-event-gate-functional>")) {
            g_assert_null(path);
            path = g_strdup_printf("/machine/unattached/%s",
                                    qdict_get_str(child, "name"));
        }
    }
    qobject_unref(reply);
    g_assert_nonnull(path);
    return path;
}

static void paired_gate_slot(QTestState *q, unsigned width)
{
    const uint32_t words[] = {
        0x09000201, 0x15500, 0, 0, 64, 64, 0, 0,
        (28 << 16) | width, 0, 0x8000, 0, 0,
    };

    for (unsigned i = 0; i < ARRAY_SIZE(words); i++) {
        qtest_writel(q, PAIRED_GATE + 0x600 + 4 * i, words[i]);
    }
    qtest_writel(q, PAIRED_GATE + 0x2c, 8);
}

static void paired_gate_input(QTestState *q, unsigned upstream)
{
    uint8_t packet[356] = { 0 };

    stl_le_p(packet, 0x11040000);
    stl_le_p(packet + 4, upstream);
    stl_le_p(packet + 16, 0x11510200);
    stl_le_p(packet + 340, 0x01040400);
    qtest_memwrite(q, RAM + 0x150000, packet, sizeof(packet));
    qtest_writel(q, PAIRED_GATE + 0xe74, 1);
    qtest_writel(q, PAIRED_GATE + 0xe74, 0);
    qtest_writel(q, PAIRED_GATE + 0xe70, RAM + 0x150000);
}

static void paired_stream(QTestState *q, uint32_t base)
{
    paired_photo(q, base);
    qtest_writel(q, base + 0x424, 3);
    for (unsigned phase = 0; phase < 4; phase++) {
        qtest_writel(q, base + 0x488 + 4 * phase, 0x40000000);
        qtest_writel(q, base + 0x498 + 4 * phase, 0x40008000);
    }
}

static void paired_stream_pixels(QTestState *q, const char *path, bool present)
{
    QDict *reply = qtest_qmp(q, "{'execute':'qom-get','arguments':"
                               "{'path':%s,'property':'input-data'}}", path);
    gsize length;
    g_autofree guchar *pixels = g_base64_decode(qdict_get_str(reply, "return"),
                                               &length);

    g_assert_cmpuint(length, ==, present ? 56 * 42 : 0);
    if (present) {
        for (unsigned i = 0; i < 56 * 42; i++) {
            g_assert_cmpuint(pixels[i], ==, i < 56 * 28 ? 129 :
                             i % 2 ? 96 : 144);
        }
    }
    qobject_unref(reply);
}

static void test_paired_stream(void)
{
    g_autofree char *bundle = NULL;
    g_autofree char *scene = NULL;
    QTestState *q = paired_start(&bundle, &scene, true, false);
    g_autofree char *gate = paired_gate_path(q);

    paired_gate_slot(q, 56);
    paired_gate_input(q, 8);
    paired_stream(q, PAIRED);
    paired_stream_pixels(q, gate, false);
    qtest_writel(q, PAIRED + 0x10, 1);
    qtest_writel(q, PAIRED + 8, 1);
    work_input(q);
    work_rise(q);
    qtest_clock_step(q, 2000000);
    g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 1);
    g_assert_cmphex(qtest_readl(q, PAIRED + 0x24), ==, 0x1002);
    paired_pixels(q, PAIRED_GRID, 0);
    paired_stream_pixels(q, gate, true);
    g_assert_cmphex(qtest_readl(q, PAIRED_GATE + 0x2060), ==, 1);
    paired_stream_pixels(q, gate, true); /* Diagnostics never consume input. */
    for (unsigned word = 0; word < 3; word++) {
        g_assert_cmphex(qtest_readl(q, PAIRED_GATE + 0x18 + 4 * word), ==, 0);
    }
    g_assert_false(qtest_get_irq(q, 102 - 32));
    g_assert_cmphex(qtest_readl(q, PAIRED_GATE + 0xe60), ==, 0);
    paired_gate_slot(q, 56); /* Same committed shape preserves ownership. */
    qtest_writel(q, PAIRED + 0x24, UINT32_MAX);
    work_ack(q);
    work_input(q);
    work_rise(q);
    qtest_clock_step(q, 2000000);
    g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 1);
    g_assert_cmpuint(property_at(q, PAIRED_PATH, "blocked"), >, 0);
    g_assert_cmphex(qtest_readl(q, PAIRED + 0x24), ==, 0);
    paired_stream_pixels(q, gate, true);
    paired_gate_slot(q, 54);
    paired_stream_pixels(q, gate, false);
    g_assert_cmphex(qtest_readl(q, PAIRED_GATE + 0x2060), ==, 0);
    g_assert_cmphex(qtest_readl(q, PAIRED_GATE + 0x18), ==, 0);
    qtest_quit(q);
    unlink(bundle);
    unlink(scene);
}

static void test_paired_stream_cancel(void)
{
    for (unsigned mode = 0; mode < 5; mode++) {
        g_autofree char *bundle = NULL;
        g_autofree char *scene = NULL;
        QTestState *q = paired_start(&bundle, &scene, true, false);
        g_autofree char *gate = paired_gate_path(q);

        paired_gate_slot(q, mode == 4 ? 54 : 56);
        paired_gate_input(q, mode == 3 ? 9 : 8);
        paired_stream(q, PAIRED);
        qtest_writel(q, PAIRED + 0x10, 1);
        qtest_writel(q, PAIRED + 8, 1);
        work_input(q);
        work_rise(q);
        qtest_clock_step(q, 1000000);
        if (mode == 0) {
            paired_gate_slot(q, 54);
        } else if (mode == 1) {
            qtest_writel(q, PAIRED_GATE + 0xe74, 1);
        } else if (mode == 2) {
            qtest_system_reset(q);
        }
        qtest_clock_step(q, 2000000);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 0);
        paired_stream_pixels(q, gate, false);
        paired_untouched(q);
        g_assert_cmphex(qtest_readl(q, PAIRED_GATE + 0x18), ==, 0);
        qtest_quit(q);
        unlink(bundle);
        unlink(scene);
    }
}

static void test_paired_stream_peer(void)
{
    const uint32_t peer = PAIRED + 0x40000;
    g_autofree char *bundle = NULL;
    g_autofree char *scene = NULL;
    QTestState *q = paired_start(&bundle, &scene, true, false);
    g_autofree char *gate = paired_gate_path(q);

    paired_configure(q, true, peer);
    for (unsigned i = 0; i < 4; i++) {
        qtest_writel(q, peer + 0x398 + i * 4, RAM + 0x160000 + i * 0x1000);
    }
    paired_gate_slot(q, 56);
    paired_gate_input(q, 8);
    paired_stream(q, PAIRED);
    paired_stream(q, peer);
    qtest_writel(q, PAIRED + 0x10, 1);
    qtest_writel(q, PAIRED + 8, 1);
    qtest_writel(q, peer + 0x10, 1);
    qtest_writel(q, peer + 8, 1);
    work_input(q);
    work_rise(q);
    qtest_clock_step(q, 1000000);
    g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 0);
    g_assert_cmpuint(property_at(q, PAIRED_PATH, "blocked"), >, 0);
    qtest_clock_step(q, 1000000);
    g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 1);
    paired_stream_pixels(q, gate, true);
    qtest_writel(q, PAIRED + 0x24, UINT32_MAX);
    qtest_clock_step(q, 2000000);
    g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 1);
    g_assert_cmphex(qtest_readl(q, peer + 0x24), ==, 0);
    g_assert_cmpuint(property_at(q, gate, "input-frames"), ==, 1);
    g_assert_cmphex(qtest_readl(q, RAM + 0x160000), ==, 0);
    paired_stream_pixels(q, gate, true);
    qtest_quit(q);
    unlink(bundle);
    unlink(scene);
}

static void test_paired_stream_lifecycle(void)
{
    for (unsigned mode = 0; mode < 5; mode++) {
        g_autofree char *bundle = NULL;
        g_autofree char *scene = NULL;
        QTestState *q = paired_start(&bundle, &scene, true, false);
        g_autofree char *gate = paired_gate_path(q);

        paired_gate_slot(q, 56);
        paired_gate_input(q, 8);
        paired_stream(q, PAIRED);
        qtest_writel(q, PAIRED + 0x10, 1);
        qtest_writel(q, PAIRED + 8, 1);
        work_input(q);
        work_rise(q);
        qtest_clock_step(q, 2000000);
        paired_stream_pixels(q, gate, true);
        if (mode == 0) {
            paired_gate_input(q, 8);
        } else if (mode == 1) {
            qtest_writel(q, PAIRED_GATE + 4, 0);
        } else if (mode == 2) {
            qtest_system_reset(q);
        } else if (mode == 3) {
            qtest_writel(q, PAIRED_GATE + 0x620, (28 << 16) | 54);
        } else {
            paired_gate_slot(q, 56);
        }
        qtest_clock_step(q, 2000000);
        paired_stream_pixels(q, gate, mode >= 3);
        g_assert_cmpuint(property_at(q, gate, "input-frames"), ==, mode != 2);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 1);
        qtest_quit(q);
        unlink(bundle);
        unlink(scene);
    }
}

static void paired_stop(QTestState *q, uint32_t base)
{
    static const unsigned commands[] = { 0x54, 0x60, 0x6c, 0x78 };

    qtest_writel(q, base + 0x84, 0);
    qtest_writel(q, base + 0xf0, 0xfc400);
    for (unsigned i = 0; i < ARRAY_SIZE(commands); i++) {
        qtest_writel(q, base + commands[i], 1);
    }
}

static void paired_first_batch(QTestState *q, bool acknowledge_last)
{
    for (unsigned phase = 0; phase < 4; phase++) {
        work_input(q);
        work_rise(q);
        qtest_clock_step(q, 2000000);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==,
                         phase + 1);
        paired_pixels(q, PAIRED_MAIN, phase % 2);
        paired_pixels(q, PAIRED_WORK, phase % 2);
        paired_pixels(q, PAIRED_GRID, phase);
        if (phase != 3 || acknowledge_last) {
            qtest_writel(q, PAIRED + 0x24, UINT32_MAX);
        }
        work_ack(q);
    }
}

static void test_paired_selections(void)
{
    static const uint8_t gray[] = {
        'P', '5', '\n', '1', ' ', '1', '\n', '2', '5', '5', '\n', 129,
    };
    const uint32_t extra = 0x4000;
    g_autofree char *bundle = NULL;
    g_autofree char *scene = NULL;
    QTestState *q = paired_start(&bundle, &scene, true, true);

    for (unsigned p = 0; p < 4; p++) {
        qtest_memset(q, WORK_BUFFER + extra + p * 0x1000 - 16, 0xa5, 0xda0);
        qtest_memset(q, PAIRED_GRID + extra + p * 0x1000 - 16, 0xa5, 0xce0);
    }
    for (unsigned p = 0; p < 2; p++) {
        qtest_memset(q, PAIRED_MAIN + extra + p * 0x2000 - 16, 0xa5, 0xce0);
    }
    work_input(q);
    work_rise(q);
    qtest_clock_step(q, 1000000);
    /* Inactive descriptor staging cannot cancel the snapshotted primary job. */
    paired_secondary(q, extra);
    qtest_clock_step(q, 1000000);
    g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 1);
    g_assert_cmpuint(property_at(q, PAIRED_PATH, "cancelled"), ==, 0);
    paired_pixels(q, PAIRED_MAIN, 0);
    /* Two partial batches cannot be spliced into one four-read watermark. */
    qtest_writel(q, PAIRED + 0xfc, 1);
    g_assert_cmphex(qtest_readl(q, PAIRED + 0xfc), ==, 0);
    g_assert_cmphex(qtest_readl(q, PAIRED + 0x24), ==, 0x1002);
    qtest_writel(q, PAIRED + 0x24, UINT32_MAX);
    work_ack(q);
    for (unsigned phase = 1; phase < 4; phase++) {
        work_input(q);
        work_rise(q);
        qtest_clock_step(q, 2000000);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"),
                         ==, phase + 1);
        paired_pixels(q, PAIRED_MAIN, phase % 2);
        qtest_writel(q, PAIRED + 0x24, UINT32_MAX);
        work_ack(q);
    }
    qtest_writel(q, PAIRED + 0xfc, 1);
    qtest_writel(q, PAIRED + 0x10, 1);
    qtest_clock_step(q, 2000000);
    g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 4);
    g_assert_cmphex(qtest_readl(q, PAIRED_MAIN + extra), ==, 0xa5a5a5a5);
    for (unsigned i = 0; i < 4; i++) {
        qtest_writel(q, 0x20069a50 + 4 * i, WORK_BUFFER + extra + 0x1000 * i);
    }
    qtest_writel(q, CAPTURE + 0x104, 0x80);
    g_assert_true(g_file_set_contents(scene, (const char *)gray,
                                      sizeof(gray), NULL));
    qtest_qmp_assert_success(q,
        "{'execute':'qom-set','arguments':{'path':'/machine/cmos',"
        "'property':'image','value':%s}}", scene);
    for (unsigned phase = 0; phase < 4; phase++) {
        unsigned slot = (phase + 1) % 4;

        work_input(q);
        work_rise(q);
        qtest_clock_step(q, 2000000);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"),
                         ==, phase + 5);
        g_assert_cmphex(qtest_readl(q, WORK_BUFFER + extra + slot * 0x1000),
                        ==, 0x18818818);
        g_assert_cmphex(qtest_readl(q, WORK_BUFFER + slot * 0x1000),
                        ==, 0x08808808);
        paired_main_at(q, PAIRED_MAIN + extra + (phase % 2) * 0x2000,
                       130, 144, 96);
        g_assert_cmphex(qtest_readl(q, PAIRED_GRID + extra + phase * 0x1000),
                        ==, 0x18818818);
        g_assert_cmpuint(qtest_readw(q, PAIRED_WORK + (phase % 2) * 0x2000),
                        ==, 2072);
        paired_pixels(q, PAIRED_MAIN, phase % 2);
        paired_pixels(q, PAIRED_GRID, phase);
        qtest_writel(q, PAIRED + 0x24, UINT32_MAX);
        work_ack(q);
    }
    g_assert_cmpuint(property_at(q, PAIRED_PATH, "rejected"), ==, 0);
    qtest_quit(q);
    unlink(scene);
    unlink(bundle);
}

static void paired_second_batch(bool update)
{
    static const uint8_t gray[] = {
        'P', '5', '\n', '1', ' ', '1', '\n', '2', '5', '5', '\n', 129,
    };
    g_autofree char *bundle = NULL;
    g_autofree char *scene = NULL;
    QTestState *q = paired_start(&bundle, &scene, true, true);
    uint8_t expected[0xce0], actual[sizeof(expected)];

    paired_first_batch(q, true);
    /* A new source epoch removes numerical history without changing targets. */
    qtest_writel(q, CAPTURE + 0x500, 1);
    qtest_writel(q, CAPTURE + 0x100, 1);
    qtest_writel(q, CAPTURE + 0x500, 0);
    qtest_writel(q, CAPTURE + 0x100, 1);
    g_assert_true(g_file_set_contents(scene, (const char *)gray,
                                      sizeof(gray), NULL));
    qtest_qmp_assert_success(q,
        "{'execute':'qom-set','arguments':{'path':'/machine/cmos',"
        "'property':'image','value':%s}}", scene);
    qtest_memset(q, PAIRED_MAIN - 16, 0xa5, sizeof(expected));
    work_input(q);
    work_rise(q);
    qtest_clock_step(q, 2000000);
    g_assert_cmpuint(property_at(q, WORK_PATH, "completed"), ==, 5);
    g_assert_cmphex(qtest_readl(q, WORK_BUFFER + 0x1000), ==, 0x18818818);
    g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 4);
    g_assert_cmphex(qtest_readl(q, PAIRED + 0x24), ==, 0);
    g_assert_cmphex(qtest_readl(q, PAIRED_MAIN), ==, 0xa5a5a5a5);
    if (update) {
        for (unsigned phase = 0; phase < 4; phase++) {
            qtest_writel(q, PAIRED + 0x310 + 4 * phase,
                         WORK_BUFFER + ((phase + 1) % 4) * 0x1000);
            qtest_writel(q, PAIRED + 0x218 + 4 * phase,
                         WORK_BUFFER + ((phase + 1) % 4) * 0x1000);
            if (phase != 3) {
                /* A partial update never releases the next real RAW. */
                qtest_writel(q, PAIRED + 0x10, 1);
                qtest_clock_step(q, 2000000);
                g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"),
                                 ==, 4);
                g_assert_cmphex(qtest_readl(q, PAIRED + 0x24), ==, 0);
                g_assert_cmphex(qtest_readl(q, PAIRED_MAIN), ==, 0xa5a5a5a5);
            }
        }
    }
    /* Both a stable ring and a complete update still need the source's kick. */
    qtest_clock_step(q, 2000000);
    g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 4);
    qtest_writel(q, PAIRED + 0x10, 1);
    qtest_writel(q, PAIRED + 0x10, 1); /* Duplicate active kick is inert. */
    qtest_clock_step(q, 2000000);
    g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 5);
    memset(expected, 0xa5, sizeof(expected));
    for (unsigned y = 0; y < 28; y++) {
        memset(expected + 16 + 64 * y, 130, 56);
    }
    for (unsigned y = 0; y < 14; y++) {
        for (unsigned x = 0; x < 56; x += 2) {
            expected[16 + 64 * (28 + y) + x] = 144;
            expected[16 + 64 * (28 + y) + x + 1] = 96;
        }
    }
    qtest_memread(q, PAIRED_MAIN - 16, actual, sizeof(actual));
    g_assert_cmpmem(actual, sizeof(actual), expected, sizeof(expected));
    g_assert_cmphex(qtest_readw(q, PAIRED_WORK), ==, 2072);
    g_assert_cmphex(qtest_readw(q, PAIRED_WORK + 80 * 18), ==, 0);
    g_assert_cmphex(qtest_readl(q, PAIRED_GRID) & 0xffffff, ==, 0x818818);
    g_assert_cmphex(qtest_readl(q, PAIRED + 0x118), ==, 5 * 56 * 28);
    g_assert_cmphex(qtest_readl(q, PAIRED + 0x24), ==, 0x1002);
    qtest_writel(q, PAIRED + 0x10, 1);
    qtest_clock_step(q, 2000000);
    g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 5);
    qtest_quit(q);
    unlink(scene);
    unlink(bundle);
}

static void test_paired_second_batch(void)
{
    paired_second_batch(false);
    paired_second_batch(true);
}

static void test_paired_stop_drain_race(void)
{
    g_autofree char *bundle = NULL;
    g_autofree char *scene = NULL;
    QTestState *q = paired_start(&bundle, &scene, true, true);
    static const unsigned commands[] = { 0x54, 0x60, 0x6c, 0x78 };

    paired_first_batch(q, false);
    g_assert_cmphex(qtest_readl(q, PAIRED + 0x24), ==, 0x1402);
    qtest_writel(q, PAIRED + 0xc, 1);
    qtest_writel(q, PAIRED + 0x84, 0);
    qtest_writel(q, PAIRED + 0x24, UINT32_MAX);
    g_assert_cmphex(qtest_readl(q, PAIRED + 0x24), ==, 0);
    g_assert_cmphex(qtest_readl(q, PAIRED + 0x18), ==, 1);
    qtest_writel(q, PAIRED + 0x10, 1);
    qtest_writel(q, PAIRED + 8, 1);
    qtest_clock_step(q, 2000000);
    g_assert_cmphex(qtest_readl(q, PAIRED + 0x24), ==, 0);
    qtest_writel(q, PAIRED + 0xf0, 0xfc400);
    for (unsigned i = 0; i < ARRAY_SIZE(commands); i++) {
        qtest_writel(q, PAIRED + commands[i], 1);
        g_assert_cmphex(qtest_readl(q, PAIRED + 0x24), ==,
                        i == 3 ? 0x014a0080 : 0);
    }
    g_assert_cmphex(qtest_readl(q, PAIRED + 0x18), ==, 0);
    g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 4);
    qtest_writel(q, PAIRED + 0xe8, 0);
    g_assert_true(qtest_get_irq(q, 99 - 32));
    qtest_writel(q, PAIRED + 0x24, UINT32_MAX);
    g_assert_false(qtest_get_irq(q, 99 - 32));
    paired_pixels(q, PAIRED_MAIN, 1);
    paired_pixels(q, PAIRED_WORK, 1);
    paired_pixels(q, PAIRED_GRID, 3);
    qtest_quit(q);
    unlink(scene);
    unlink(bundle);
}

static void test_paired_peer_release(void)
{
    const uint32_t peer = 0x203c0000;
    g_autofree char *bundle = NULL;
    g_autofree char *scene = NULL;
    QTestState *q = paired_start(&bundle, &scene, true, true);

    paired_configure(q, true, peer);
    qtest_writel(q, peer + 0x10, 1);
    qtest_writel(q, peer + 8, 1);
    work_input(q);
    work_rise(q);
    qtest_clock_step(q, 1000000);
    g_assert_cmpuint(property_at(q, WORK_PATH, "completed"), ==, 1);
    g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 0);
    g_assert_cmpuint(property_at(q, PAIRED_PATH, "blocked"), >, 0);
    paired_stop(q, PAIRED);
    /* The peer retries owned RAW without a new input edge. */
    qtest_clock_step(q, 1000000);
    g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 1);
    g_assert_cmpuint(property_at(q, PAIRED_PATH, "cancelled"), ==, 1);
    g_assert_cmphex(qtest_readl(q, PAIRED + 0x24), ==, 0x014a0080);
    g_assert_cmphex(qtest_readl(q, peer + 0x24), ==, 0x1002);
    paired_pixels(q, PAIRED_MAIN, 0);
    paired_pixels(q, PAIRED_WORK, 0);
    paired_pixels(q, PAIRED_GRID, 0);
    qtest_writel(q, peer + 0xe8, 0);
    g_assert_false(qtest_get_irq(q, 99 - 32));
    g_assert_true(qtest_get_irq(q, 100 - 32));
    qtest_writel(q, peer + 0x24, UINT32_MAX);
    g_assert_false(qtest_get_irq(q, 100 - 32));
    qtest_quit(q);
    unlink(scene);
    unlink(bundle);
}

static void test_capture_geometry_reduction(void)
{
    static const uint32_t rows[] = { 36, 35, 73 };
    static const char header[] = "P5\n1 72\n255\n";

    for (unsigned mode = 0; mode < ARRAY_SIZE(rows); mode++) {
        g_autofree char *bundle = NULL;
        g_autofree char *scene = NULL;
        QTestState *q = paired_start(&bundle, &scene, true, false);
        uint8_t gradient[sizeof(header) - 1 + 72];
        uint8_t expected[0xda0], actual[sizeof(expected)];

        memcpy(gradient, header, sizeof(header) - 1);
        for (unsigned y = 0; y < 72; y++) {
            gradient[sizeof(header) - 1 + y] = 3 * y;
        }
        g_assert_true(g_file_set_contents(scene, (const char *)gradient,
                                          sizeof(gradient), NULL));
        qtest_qmp_assert_success(q,
            "{'execute':'qom-set','arguments':{'path':'/machine/cmos',"
            "'property':'image','value':%s}}", scene);
        qtest_writel(q, CAPTURE + 0x1d00, 72 << 16);
        qtest_writel(q, CAPTURE + 0x1d04, 60 << 16);
        qtest_writel(q, CAPTURE + 0x1e78, 72 << 16);
        qtest_writel(q, CAPTURE + 0x1e7c, 60 << 16);
        qtest_writel(q, 0x20069518, 60 << 16);
        qtest_writel(q, 0x2006951c, rows[mode] << 16);
        qtest_writel(q, CAPTURE + 0x100, 0x85);
        pipeline_configure(q, RAM, 256, 256 * 72,
                           NIKON_READOUT_C170_FORMAT_PRIMARY);
        pipeline_request(q);
        qtest_clock_step(q, 10000);
        qtest_writel(q, CAPTURE + 0x210, qtest_readl(q, CAPTURE + 0x210));
        work_rise(q);
        qtest_clock_step(q, 2000000);
        memset(expected, 0xa5, sizeof(expected));
        if (mode == 0) {
            /* The whole72-row image is sampled, not its first36-row crop. */
            for (unsigned y = 0; y < 36; y++) {
                unsigned value = (6 * y * 257) >> 4;
                uint8_t pair[] = {
                    value, (value >> 8) | (value << 4), value >> 4,
                };

                for (unsigned x = 0; x < 90; x += 3) {
                    memcpy(expected + 16 + 96 * y + x, pair, sizeof(pair));
                }
            }
        }
        qtest_memread(q, WORK_BUFFER + 0x1000 - 16, actual, sizeof(actual));
        g_assert_cmpmem(actual, sizeof(actual), expected, sizeof(expected));
        g_assert_cmpuint(property_at(q, WORK_PATH, "completed"), ==,
                         mode == 0 ? 1 : 0);
        g_assert_cmpuint(property_at(q, WORK_PATH, "rejected"), ==,
                         mode == 0 ? 0 : 1);
        paired_untouched(q);
        if (mode == 0) {
            work_ack(q);
            qtest_writel(q, 0x2006951c, 18 << 16);
            qtest_writel(q, CAPTURE + 0x104, 0x80);
            work_rise(q);
            qtest_clock_step(q, 2000000);
            g_assert_cmpuint(property_at(q, WORK_PATH, "completed"), ==, 1);
            qtest_memread(q, WORK_BUFFER + 0x1000 - 16, actual,
                          sizeof(actual));
            g_assert_cmpmem(actual, sizeof(actual), expected, sizeof(expected));
        }
        qtest_quit(q);
        unlink(scene);
        unlink(bundle);
    }
}

static void test_paired_calibration_rejected(void)
{
    static const char *const reasons[] = {
        "requires exactly", "interpreted configuration", "reserved slots",
    };

    for (unsigned mode = 0; mode < ARRAY_SIZE(reasons); mode++) {
        uint8_t calibration[0x5e00] = { 0 };
        g_autofree char *path = NULL;
        g_autofree char *machine = NULL;
        g_autofree char *errors = NULL;
        int fd = g_file_open_tmp("nikonpairedinvalidXXXXXX", &path, NULL);
        int status;

        g_assert_cmpint(fd, >=, 0);
        close(fd);
        if (mode) {
            stl_le_p(calibration + (mode == 1 ? 0x310 : 0x14), 1);
        }
        g_assert_true(g_file_set_contents(path, (const char *)calibration,
                                          sizeof(calibration) - (mode == 0),
                                          NULL));
        machine = g_strdup_printf("z7ii-mas-diagnostic,paired-calibration=%s",
                                   path);
        const char *args[] = {
            qtest_qemu_binary(NULL), "-machine", machine, "-bios", image,
            "-qtest", "stdio", "-accel", "qtest", "-display", "none",
            "-monitor", "none", "-serial", "none", "-S", NULL,
        };

        g_assert_true(g_spawn_sync(NULL, (char **)args, NULL, 0, NULL, NULL,
                                   NULL, &errors, &status, NULL));
        g_assert_false(g_spawn_check_wait_status(status, NULL));
        g_assert_nonnull(strstr(errors, reasons[mode]));
        g_assert_cmpint(unlink(path), ==, 0);
    }
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
    qtest_add_func("/nikon/cmos/image-source", test_image_source_contract);
    qtest_add_func("/nikon/cmos/image-raster", test_image_raster_boundaries);
    qtest_add_func("/nikon/cmos/pipeline/image-gray", test_image_gray_transport);
    qtest_add_func("/nikon/cmos/period", test_period);
    qtest_add_func("/nikon/cmos/pipeline/valid-silent",
                   test_pipeline_valid_and_silent);
    qtest_add_func("/nikon/cmos/pipeline/input-boundary",
                   test_pipeline_input_boundary);
    qtest_add_func("/nikon/cmos/pipeline/run-completion",
                   test_pipeline_run_completion);
    qtest_add_func("/nikon/cmos/pipeline/input-sync", test_pipeline_input_sync);
    qtest_add_func("/nikon/cmos/pipeline/unit-cancellation",
                   test_pipeline_unit_cancellation);
    qtest_add_func("/nikon/cmos/pipeline/rejections",
                   test_pipeline_rejections);
    qtest_add_func("/nikon/cmos/pipeline/backpressure-stale",
                   test_pipeline_backpressure_and_stale);
    qtest_add_func("/nikon/cmos/pipeline/cancel-reset",
                   test_pipeline_cancel_and_reset);
    qtest_add_func("/nikon/cmos/statistics/transaction",
                   test_statistics_transaction);
    qtest_add_func("/nikon/cmos/statistics/stage-completion",
                   test_statistics_stage_completion);
    qtest_add_func("/nikon/cmos/statistics/ring-completion",
                   test_statistics_ring_completion);
    qtest_add_func("/nikon/cmos/statistics/storage-period",
                   test_statistics_storage_period);
    qtest_add_func("/nikon/cmos/pipeline/main-input", test_main_input);
    qtest_add_func("/nikon/cmos/pipeline/main-lifecycle",
                   test_main_input_lifecycle);
    qtest_add_func("/nikon/cmos/pipeline/main-boundaries",
                   test_main_input_boundaries);
    qtest_add_func("/nikon/cmos/pipeline/main-masked-stream",
                   test_main_input_masked_stream);
    qtest_add_func("/nikon/cmos/statistics/saturation",
                   test_statistics_saturation);
    qtest_add_func("/nikon/cmos/statistics/lifecycle",
                   test_statistics_lifecycle);
    qtest_add_func("/nikon/cmos/statistics/boundaries",
                   test_statistics_boundaries);
    qtest_add_func("/nikon/cmos/processing/transaction", test_capture_processing);
    qtest_add_func("/nikon/cmos/processing/lifecycle", test_capture_work_lifecycle);
    qtest_add_func("/nikon/cmos/processing/boundaries",
                   test_capture_work_boundaries);
    qtest_add_func("/nikon/cmos/processing/formats", test_capture_work_formats);
    qtest_add_func("/nikon/cmos/processing/freshness", test_capture_work_freshness);
    qtest_add_func("/nikon/cmos/paired/transaction", test_paired_processing);
    qtest_add_func("/nikon/cmos/paired/preflight", test_paired_preflight);
    qtest_add_func("/nikon/cmos/paired/lifecycle", test_paired_lifecycle);
    qtest_add_func("/nikon/cmos/paired/coverage-raw",
                   test_paired_coverage_and_raw_failure);
    qtest_add_func("/nikon/cmos/paired/preserve-results",
                   test_paired_preserve_results);
    qtest_add_func("/nikon/cmos/paired/backpressure", test_paired_backpressure);
    qtest_add_func("/nikon/cmos/paired/c-matrix", test_paired_c_matrix);
    qtest_add_func("/nikon/cmos/paired/matrix-encoding",
                   test_paired_matrix_encoding);
    qtest_add_func("/nikon/cmos/paired/source-history",
                   test_paired_source_history);
    qtest_add_func("/nikon/cmos/paired/gain", test_paired_gain);
    qtest_add_func("/nikon/cmos/paired/quad", test_paired_quad);
    qtest_add_func("/nikon/cmos/paired/quad-boundaries",
                   test_paired_quad_boundaries);
    qtest_add_func("/nikon/cmos/paired/curves", test_paired_curves);
    qtest_add_func("/nikon/cmos/paired/curve-encoding",
                   test_paired_curve_encoding);
    qtest_add_func("/nikon/cmos/paired/coverage-cancellation",
                   test_paired_coverage_cancellation);
    qtest_add_func("/nikon/cmos/paired/second-batch", test_paired_second_batch);
    qtest_add_func("/nikon/cmos/paired/stop-drain-race",
                   test_paired_stop_drain_race);
    qtest_add_func("/nikon/cmos/paired/peer-release", test_paired_peer_release);
    qtest_add_func("/nikon/cmos/processing/geometry-reduction",
                   test_capture_geometry_reduction);
    qtest_add_func("/nikon/cmos/paired/calibration-rejected",
                   test_paired_calibration_rejected);
    qtest_add_func("/nikon/cmos/paired/noise", test_paired_noise);
    qtest_add_func("/nikon/cmos/paired/noise-cancel", test_paired_noise_cancel);
    qtest_add_func("/nikon/cmos/paired/selections", test_paired_selections);
    qtest_add_func("/nikon/cmos/paired/filter/transaction", test_paired_filter);
    qtest_add_func("/nikon/cmos/paired/filter/boundaries",
                   test_paired_filter_boundaries);
    qtest_add_func("/nikon/cmos/paired/filter/lifecycle",
                   test_paired_filter_lifecycle);
    qtest_add_func("/nikon/cmos/paired/photo", test_paired_photo);
    qtest_add_func("/nikon/cmos/paired/stride", test_paired_stride);
    qtest_add_func("/nikon/cmos/paired/stream/transaction", test_paired_stream);
    qtest_add_func("/nikon/cmos/paired/stream/cancel",
                   test_paired_stream_cancel);
    qtest_add_func("/nikon/cmos/paired/stream/peer", test_paired_stream_peer);
    qtest_add_func("/nikon/cmos/paired/stream/lifecycle",
                   test_paired_stream_lifecycle);
    result = g_test_run();
    unlink(image);
    g_free(image);
    unlink(source);
    g_free(source);
    return result;
}

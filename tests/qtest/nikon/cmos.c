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
    QTestState *q = pipeline_start(&bundle);

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
    QTestState *q = pipeline_start(&bundle);
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
    QTestState *q = pipeline_start(&bundle);

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
    QTestState *q = pipeline_start(&bundle);

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
        QTestState *q = pipeline_start(&bundle);

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
    QTestState *q = pipeline_start(&bundle);

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
    QTestState *q = pipeline_start(&bundle);

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
    QTestState *q = pipeline_start(&bundle);

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

static void test_statistics_ring_completion(void)
{
    g_autofree char *bundle = NULL;
    QTestState *q = pipeline_start(&bundle);

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
    QTestState *q = pipeline_start(&bundle);

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
    QTestState *q = pipeline_start(&bundle);

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
        QTestState *q = pipeline_start(&bundle);

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
    QTestState *q = pipeline_start(&bundle);

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
    QTestState *q = pipeline_start(&bundle);

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
    QTestState *q = pipeline_start(&bundle);
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
    QTestState *q = pipeline_start(&bundle);
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
    QTestState *q = pipeline_start(&bundle);

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
        QTestState *q = pipeline_start(&bundle);

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
    QTestState *q = pipeline_start(&bundle);

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
    QTestState *q = pipeline_start(&bundle);
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
        QTestState *q = pipeline_start(&bundle);

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
        QTestState *q = pipeline_start(&bundle);

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
        QTestState *q = pipeline_start(&bundle);
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
    QTestState *q = pipeline_start(&bundle);

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
    result = g_test_run();
    unlink(image);
    g_free(image);
    unlink(source);
    g_free(source);
    return result;
}

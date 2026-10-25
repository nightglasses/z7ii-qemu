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
        { 0x2006a278, 0 },          /* empty geometry */
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
            qtest_writel(q, 0x2006ad3c, 2);
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
    qtest_add_func("/nikon/cmos/pipeline/input-boundary",
                   test_pipeline_input_boundary);
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
    qtest_add_func("/nikon/cmos/statistics/transaction",
                   test_statistics_transaction);
    qtest_add_func("/nikon/cmos/statistics/saturation",
                   test_statistics_saturation);
    qtest_add_func("/nikon/cmos/statistics/lifecycle",
                   test_statistics_lifecycle);
    qtest_add_func("/nikon/cmos/statistics/boundaries",
                   test_statistics_boundaries);
    result = g_test_run();
    unlink(image);
    g_free(image);
    unlink(source);
    g_free(source);
    return result;
}

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
static char *paired_gate_path(QTestState *q);

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

static void pipeline_step(QTestState *q, int64_t nanoseconds)
{
    int64_t deadline = g_get_monotonic_time() + 10 * G_TIME_SPAN_SECOND;
    int64_t now;
    g_autofree char *gate = paired_gate_path(q);

    now = qtest_clock_step(q, nanoseconds);
    /* Drain host math and due virtual publications/capacity wakes at this
     * fixed virtual time. A validated ready result may wait for real capacity;
     * no future1ms deadline is advanced merely to make the helper return.
     */
    for (;;) {
        /* Read upstream workers before paired's wake/ready state: a gate
         * completion between these reads either remains inflight here or has
         * already scheduled the capacity timer observed below.
         */
        bool pending;
        unsigned gate_compute;
        QDict *reply;
        const char *state, *inflight, *capacity, *bank;

        pending = property_at(q, "/machine/capture-group", "inflight") != 0;
        pending |= property_at(q, "/machine/capture-processing", "inflight") != 0;
        pending |= property_at(q, "/machine/capture-statistics", "inflight") != 0;
        gate_compute = property_at(q, gate, "processing-compute");
        pending |= gate_compute == 1;
        if (gate_compute == 2) {
            /* Deliver the normal due ready-result check once. A gate result
             * held behind real pending ACKs may stay ready without host math.
             */
            qtest_clock_set(q, now);
        }
        reply = qtest_qmp(q,
            "{'execute':'qom-get','arguments':"
            "{'path':'/machine/paired-processing','property':'runtime'}}");
        state = qdict_get_str(reply, "return");
        inflight = strstr(state, " inflight=");
        capacity = strstr(state, " capacity-pending=");
        bank = state;

        g_assert_nonnull(inflight);
        g_assert_nonnull(capacity);
        pending |= strtoul(capacity + strlen(" capacity-pending="), NULL, 10) != 0;
        while ((bank = strstr(bank, " compute="))) {
            unsigned compute = strtoul(bank + strlen(" compute="), NULL, 10);
            const char *wait = strstr(bank, " capacity-wait=");
            const char *end = strchr(bank, '\n');

            g_assert_cmpuint(compute, <=, 2);
            g_assert_nonnull(wait);
            g_assert_nonnull(end);
            g_assert_true(wait < end);
            /* Ready BUSY results intentionally retain their aggregate. Every
             * other ready result still needs its normal virtual-clock check.
             */
            pending |= compute == 1 || (compute == 2 &&
                !strtoul(wait + strlen(" capacity-wait="), NULL, 10));
            bank = end;
        }
        qobject_unref(reply);
        if (!pending) {
            return;
        }
        g_assert_cmpint(g_get_monotonic_time(), <, deadline);
        qtest_clock_set(q, now);
    }
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

static void test_pipeline_cpu_lifecycle(void)
{
    for (unsigned instance = 0; instance < 4; instance++) {
        g_autofree char *bundle = NULL;
        QTestState *q = pipeline_start(&bundle, NULL);
        QDict *reply = qtest_qmp(q, "{'execute':'query-cpus-fast'}");

        /* Two MAS, two SLA and one EX DUMMY thread share the qtest clock. */
        g_assert_cmpuint(qlist_size(qdict_get_qlist(reply, "return")), ==, 5);
        qobject_unref(reply);
        for (unsigned cycle = 0; cycle < 32; cycle++) {
            for (unsigned poll = 0; poll < 8; poll++) {
                /* Each EX millisecond poll synchronously queues real CPU work. */
                qtest_clock_step(q, 1000000);
            }
            reply = qtest_qmp(q, "{'execute':'qom-get','arguments':"
                             "{'path':'/machine/ex',"
                             "'property':'first-exception'}}");
            g_assert_false(qdict_get_bool(reply, "return"));
            qobject_unref(reply);

            qtest_qmp_assert_success(q, "{'execute':'stop'}");
            qtest_qmp_eventwait(q, "STOP");
            reply = qtest_qmp(q, "{'execute':'query-status'}");
            g_assert_false(qdict_get_bool(qdict_get_qdict(reply, "return"),
                                          "running"));
            qobject_unref(reply);
            qtest_qmp_assert_success(q, "{'execute':'cont'}");
            qtest_qmp_eventwait(q, "RESUME");
            qtest_clock_step(q, 1000000);
            if (cycle % 8 == 7) {
                qtest_system_reset(q);
                qtest_clock_step(q, 1000000);
            }
        }
        /* libqtest checks normal exit; a forced SIGKILL is still a failure. */
        qtest_quit(q);
        unlink(bundle);
    }
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
    pipeline_step(q, 10000);
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
    pipeline_step(q, 10000);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "completed"), ==, 1);
    g_assert_cmphex(qtest_readl(q, RAM), !=, 0xa5a5a5a5);
    g_assert_cmphex(qtest_readl(q, RAM + 0x20000), ==, 0x5a5a5a5a);
    g_assert_cmphex(qtest_readl(q, CAPTURE + 0x210), ==, 8);

    /* Neither a consumed edge nor a repeated high level admits a new job. */
    qtest_set_irq_in(q, PIPELINE_PATH, "automatic-rise", 0, 1);
    qtest_writel(q, READOUT_PARAMETERS + 0x400, 1);
    qtest_set_irq_in(q, PIPELINE_PATH, "automatic-rise", 0, 1);
    pipeline_step(q, 10000);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "completed"), ==, 1);
    qtest_writel(q, CAPTURE + 0xc, 2);
    pipeline_input(q);
    pipeline_step(q, 10000);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "cancelled"), ==, 1);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "active-generation"), ==, 0);
    g_assert_cmphex(qtest_readl(q, RAM + 0x20000), ==, 0x5a5a5a5a);

    qtest_writel(q, CAPTURE + 8, 2);
    qtest_writel(q, READOUT_PARAMETERS + 0x400, 1);
    qtest_system_reset(q);
    pipeline_input(q);
    pipeline_step(q, 10000);
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
    pipeline_step(q, 100000000);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "completed"), ==, 1);
    qtest_writel(q, 0x20060070, 0);
    pipeline_step(q, 16666666);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "completed"), ==, 1);
    pipeline_step(q, 10000);
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

static void test_image_cfa_transport(void)
{
    static const char header[] = "P6\r\n2 2\r\n255\r\n";
    static const uint8_t colors[][3] = {
        { 255, 0, 0 }, { 0, 255, 0 }, { 0, 0, 255 }, { 255, 255, 255 },
    };
    g_autofree char *bundle = NULL;
    g_autofree char *path = NULL;
    QTestState *q = pipeline_start(&bundle, NULL);
    uint8_t ppm[sizeof(header) - 1 + 4 * 3];
    uint8_t output[512];
    int fd = g_file_open_tmp("nikon-cmos-cfa-XXXXXX", &path, NULL);

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
    pipeline_step(q, 10000);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "completed"), ==, 1);
    qtest_memread(q, RAM, output, sizeof(output));
    for (unsigned i = 0; i < 256; i++) {
        unsigned x = i % 128, y = i / 128;
        unsigned lane = 2 * (y % 2) + x % 2;
        unsigned channel = lane == 0 ? 0 : lane == 3 ? 2 : 1;

        g_assert_cmphex(lduw_le_p(output + 2 * i), ==,
                        colors[2 * y + x / 64][channel] * 257u);
    }
    statistics_rise(q);
    pipeline_step(q, 2000000);
    g_assert_cmpuint(property_at(q, "/machine/capture-statistics",
                                 "completed"), ==, 1);
    g_assert_cmphex(qtest_readl(q, CAPTURE + 0x270), ==, 0x20);
    configure(q, 128, 2, 1, 128);
    qtest_writel(q, CMOS + 0x024, NIKON_CMOS_FORMAT_FUNCTIONAL_C170);
    qtest_writel(q, CMOS + 0x060, 1);
    g_assert_cmpuint(qtest_readl(q, CMOS + 0x068), ==, NIKON_CMOS_ERROR);
    qtest_quit(q);
    unlink(bundle);
    unlink(path);
}

static void test_image_cfa_response(void)
{
    static const unsigned response[] = { 2048, 0, 4097 };
    static const uint8_t ppm[] = "P6\n1 1\n255\n\x80\x80\x80";
    g_autofree char *path = NULL;
    g_autofree char *bios = g_shell_quote(image);
    int fd = g_file_open_tmp("nikon-cmos-response-XXXXXX", &path, NULL);

    g_assert_cmpint(fd, >=, 0);
    close(fd);
    g_assert_true(g_file_set_contents(path, (const char *)ppm,
                                      sizeof(ppm) - 1, NULL));
    for (unsigned i = 0; i < ARRAY_SIZE(response); i++) {
        QTestState *q = qtest_initf(
            "-M z7ii-mas-diagnostic -smp 1 -bios %s -nodefaults -display none "
            "-global nikon-cmos-functional.red-response=%u "
            "-global nikon-cmos-functional.blue-response=1024", bios, response[i]);

        configure(q, 2, 2, 2, 8);
        qtest_qmp_assert_success(q,
            "{'execute':'qom-set','arguments':{'path':'/machine/cmos',"
            "'property':'image','value':%s}}", path);
        qtest_writel(q, CMOS + 0x024, NIKON_CMOS_FORMAT_FUNCTIONAL_C170);
        qtest_memset(q, RAM, 0xa5, 16);
        qtest_writel(q, CMOS + 0x060, 1);
        qtest_clock_step(q, 10000);
        g_assert_cmpuint(qtest_readl(q, CMOS + 0x068), ==,
                         i ? NIKON_CMOS_ERROR : NIKON_CMOS_DONE);
        if (!i) {
            g_assert_cmphex(qtest_readl(q, RAM), ==, 0x80804040);
            g_assert_cmphex(qtest_readl(q, RAM + 8), ==, 0x20208080);
            g_assert_cmphex(qtest_readl(q, RAM + 4), ==, 0xa5a5a5a5);
        } else {
            g_assert_cmphex(qtest_readl(q, RAM), ==, 0xa5a5a5a5);
        }
        qtest_system_reset(q);
        g_assert_cmpuint(qtest_readl(q, CMOS + 0x068), ==, NIKON_CMOS_IDLE);
        qtest_quit(q);
    }
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
    pipeline_step(q, 10000);
    g_assert_cmphex(qtest_readl(q, RAM), ==, 0xa5a5a5a5);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "requests"), ==, 0);

    pipeline_request(q);
    g_assert_cmphex(qtest_readl(q, READOUT_PARAMETERS + 0x3c0), ==, 1);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "accepted"), ==, 1);
    pipeline_step(q, 10000);
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
    pipeline_step(q, 10000);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "completed"), ==, 1);
    g_assert_cmphex(qtest_readl(q, RAM), !=, 0xa5a5a5a5);
    g_assert_cmphex(qtest_readl(q, CAPTURE + 0x210), ==, 8);
    qtest_writel(q, CAPTURE + 0x210, 8);
    pipeline_request(q);
    pipeline_step(q, 10000);
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
    pipeline_step(q, 10000);
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
    pipeline_step(q, 10000);
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
    pipeline_step(q, 100);
    g_assert_cmphex(qtest_readl(q, CAPTURE + 0x210), ==, 0x20);
    qtest_writel(q, CAPTURE + 0x210, 0x20);

    /* A late start cannot turn previously acquired input into a sync. */
    qtest_writel(q, CAPTURE + 0xc, 1);
    pipeline_request(q);
    qtest_writel(q, CAPTURE + 8, 1);
    pipeline_step(q, 100);
    g_assert_cmphex(qtest_readl(q, CAPTURE + 0x210), ==, 0x20);
    qtest_writel(q, CAPTURE + 0x210, 0x20);

    pipeline_request(q);
    pipeline_step(q, 100);
    g_assert_cmphex(qtest_readl(q, CAPTURE + 0x210), ==, 0x21);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "input-notifications"), ==, 2);
    qtest_writel(q, CAPTURE + 0x210, 0x21);

    pipeline_request(q);
    qtest_writel(q, CAPTURE + 0xc, 2);
    pipeline_step(q, 100);
    g_assert_cmphex(qtest_readl(q, CAPTURE + 0x210), ==, 0);
    qtest_writel(q, CAPTURE + 8, 2);
    pipeline_request(q);
    /* Reset a separately qualified active transfer, without prior cancel. */
    qtest_system_reset(q);
    pipeline_input(q);
    pipeline_step(q, 10000);
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
    pipeline_step(q, 100);
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
        pipeline_step(q, 10000);
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
    pipeline_step(q, 10000);
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
    pipeline_step(q, 10000);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "stale"), ==, 0);
    g_assert_cmpuint(property_at(q, READOUT_PATH, "completion-generation"),
                     ==, 1);

    pipeline_request(q);
    pipeline_step(q, 10000);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "completed"), ==, 2);
    g_assert_cmpuint(property_at(q, READOUT_PATH, "completion-generation"),
                     ==, 2);
    qtest_quit(q);
    g_assert_cmpint(unlink(bundle), ==, 0);
}

static void statistics_rise(QTestState *q)
{
    qtest_set_irq_in(q, "/machine/capture-statistics", "input-ready", 0, 0);
    qtest_set_irq_in(q, "/machine/capture-statistics", "input-ready", 0, 1);
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
    pipeline_step(q, 10000);
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
    pipeline_step(q, 2000000);
    g_assert_cmphex(qtest_readl(q, CAPTURE + 0x270), ==, 0);
    g_assert_false(qtest_get_irq(q, NIKON_CAPTURE_THIRD_EVENT));

    pipeline_configure(q, RAM, 0x100, 0x400,
                       NIKON_READOUT_C170_FORMAT_PRIMARY);
    pipeline_request(q);
    pipeline_step(q, 5); /* Exposure plus four row chunks: input is now DONE. */
    pipeline_step(q, 999999);
    g_assert_cmphex(qtest_readl(q, RAM + 0x101000), ==, 0xa5a5a5a5);
    g_assert_cmphex(qtest_readl(q, CAPTURE + 0x270), ==, 0);
    pipeline_step(q, 1);
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
    pipeline_step(q, 2000000);
    g_assert_cmphex(qtest_readl(q, CAPTURE + 0x270), ==, 0);
    qtest_writel(q, 0x2006a450, 0x10020000);
    qtest_writel(q, CAPTURE + 0x100, 8);
    /* ACK now admits the blocked input, so commit its invalid route first. */
    qtest_writel(q, CAPTURE + 0x210, 0x40);
    statistics_rise(q);
    pipeline_step(q, 2000000);
    g_assert_cmphex(qtest_readl(q, CAPTURE + 0x270), ==, 0);
    g_assert_cmpuint(property_at(q, "/machine/capture-statistics",
                                 "rejected"), ==, 1);
    qtest_writel(q, 0x2006a450, RAM + 0x100000);
    qtest_writel(q, CAPTURE + 0x100, 8);
    statistics_input(q, NULL);
    statistics_rise(q);
    qtest_writel(q, CAPTURE + 0xc, 0x10);
    pipeline_step(q, 2000000);
    g_assert_cmphex(qtest_readl(q, CAPTURE + 0x270), ==, 0);
    g_assert_cmpuint(property_at(q, "/machine/capture-statistics",
                                 "cancelled"), ==, 1);
    qtest_writel(q, CAPTURE + 8, 0x10);
    statistics_input(q, NULL);
    statistics_rise(q);
    pipeline_step(q, 2000000);
    g_assert_cmphex(qtest_readl(q, CAPTURE + 0x270), ==, 0x20);
    qtest_system_reset(q);
    g_assert_cmphex(qtest_readl(q, CAPTURE + 0x270), ==, 0);
    g_assert_false(qtest_get_irq(q, NIKON_CAPTURE_THIRD_EVENT));
    pipeline_step(q, 2000000);
    g_assert_cmphex(qtest_readl(q, CAPTURE + 0x270), ==, 0);
    statistics_configure(q);
    statistics_input(q, NULL);
    statistics_rise(q);
    qtest_system_reset(q);
    pipeline_step(q, 2000000);
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
        pipeline_step(q, 1000000);
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
    pipeline_step(q, 1000000);
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
        pipeline_configure(q, RAM, 0x100, 0x400,
                           NIKON_READOUT_C170_FORMAT_PRIMARY);
        pipeline_request(q);
        pipeline_step(q, 5);
        pipeline_step(q, 999999);
        g_assert_cmphex(qtest_readl(q, CAPTURE + 0x270) & 0x80, ==, 0);
        pipeline_step(q, 1);
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
    pipeline_step(q, 1000000);
    g_assert_cmphex(qtest_readl(q, RAM + 0x200000), !=, 0xa5a5a5a5);
    g_assert_cmphex(qtest_readl(q, CAPTURE + 0x270) & 0x80, ==, 0x80);

    qtest_writel(q, CAPTURE + 0x270, 0x80);
    qtest_writel(q, CAPTURE + 0x210, 0x40);
    statistics_input(q, NULL);
    statistics_rise(q);
    qtest_writel(q, CAPTURE + 0xc, 0x10);
    pipeline_step(q, 1000000);
    g_assert_cmphex(qtest_readl(q, CAPTURE + 0x270) & 0x80, ==, 0);
    qtest_writel(q, CAPTURE + 8, 0x10);
    qtest_writel(q, 0x2006a560, 0x10020000);
    qtest_writel(q, CAPTURE + 0x100, 8);
    statistics_input(q, NULL);
    statistics_rise(q);
    pipeline_step(q, 1000000);
    g_assert_cmphex(qtest_readl(q, CAPTURE + 0x270) & 0x80, ==, 0);
    qtest_system_reset(q);
    pipeline_step(q, 1000000);
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

static void acquisition_configure(QTestState *q)
{
    qtest_qmp_assert_success(q,
        "{'execute':'qom-set','arguments':{'path':'/machine/cmos',"
        "'property':'image','value':%s}}", source);
    qtest_qmp_assert_success(q,
        "{'execute':'qom-set','arguments':{'path':'/machine/cmos',"
        "'property':'fixture-provider','value':false}}");
    main_input_configure(q);
    pipeline_configure(q, RAM, 0x100, 0x200,
                       NIKON_READOUT_C170_FORMAT_PRIMARY);
    qtest_writel(q, CAPTURE + 0x10, 2);
    qtest_writel(q, CAPTURE + 8, 2);
    qtest_memset(q, RAM, 0xa5, 0x200);
}

static void test_acquisition_views(void)
{
    g_autofree char *bundle = NULL;
    QTestState *q = pipeline_start(&bundle, NULL);

    acquisition_configure(q);
    pipeline_request(q); /* One fresh TG admits MAIN and the owned auxiliary. */
    for (unsigned i = 0; i < 8; i++) {
        pipeline_input(q); /* Busy TG edges cannot become later credits. */
    }
    pipeline_step(q, 10000);
    g_assert_cmphex(qtest_readl(q, RAM), ==, 0);
    g_assert_cmphex(qtest_readl(q, RAM + 0x1fc), ==, 0xffffffff);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "completed"), ==, 1);
    /* RED: alternating views require another TG and leave this zero. */
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "main-completed"), ==, 1);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "main-input-width"), ==, 128);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "main-input-height"), ==, 4);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "input-notifications"), ==, 2);
    g_assert_cmpuint(property_at(q, "/machine/cmos", "frame-count"), ==, 2);
    pipeline_step(q, 10000);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "main-completed"), ==, 1);
    qtest_writel(q, READOUT_PARAMETERS + 0x400, 1);
    pipeline_step(q, 10000);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "completed"), ==, 1);
    pipeline_input(q);
    pipeline_step(q, 10000);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "completed"), ==, 2);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "main-completed"), ==, 2);
    g_assert_cmpuint(property_at(q, "/machine/cmos", "frame-count"), ==, 4);
    qtest_quit(q);
    unlink(bundle);
}

static void test_acquisition_cancellation(void)
{
    for (unsigned operation = 0; operation < 5; operation++) {
        g_autofree char *bundle = NULL;
        QTestState *q = pipeline_start(&bundle, NULL);

        acquisition_configure(q);
        pipeline_request(q);
        if (operation == 0) {
            qtest_writel(q, CAPTURE + 0xc, 1); /* MAIN only. */
        } else if (operation == 1) {
            qtest_writel(q, CAPTURE + 0xc, 2); /* Queued auxiliary only. */
        } else if (operation == 2) {
            qtest_writel(q, CAPTURE + 0x1e7c, 64 << 16);
            qtest_writel(q, CAPTURE + 0x104, 4); /* Changed MAIN snapshot. */
        } else if (operation == 3) {
            qtest_system_reset(q);
        } else {
            /* MAIN's exposure plus four row chunks finish at5ns; the admitted
             * auxiliary has started but has not written its first row. */
            pipeline_step(q, 5);
            g_assert_cmpuint(property_at(q, PIPELINE_PATH, "main-completed"),
                             ==, 1);
            qtest_writel(q, CAPTURE + 0xc, 2);
        }
        pipeline_step(q, 10000);
        bool main_done = operation == 1 || operation == 4;
        bool auxiliary_done = operation == 0 || operation == 2;

        g_assert_cmpuint(property_at(q, PIPELINE_PATH, "main-completed"), ==,
                         main_done);
        g_assert_cmpuint(property_at(q, PIPELINE_PATH, "completed"), ==,
                         auxiliary_done);
        g_assert_cmphex(qtest_readl(q, RAM), ==,
                       auxiliary_done ? 0 : 0xa5a5a5a5);
        pipeline_step(q, 10000);
        g_assert_cmpuint(property_at(q, PIPELINE_PATH, "main-completed"), ==,
                         main_done);
        qtest_quit(q);
        unlink(bundle);
    }
}

static void test_acquisition_late_request(void)
{
    g_autofree char *bundle = NULL;
    QTestState *q = pipeline_start(&bundle, NULL);

    acquisition_configure(q);
    pipeline_request(q);
    /* Replace the queued auxiliary request while MAIN is already in flight. */
    qtest_writel(q, CAPTURE + 0xc, 2);
    qtest_writel(q, CAPTURE + 8, 2);
    qtest_writel(q, READOUT_PARAMETERS + 0x400, 1);
    pipeline_step(q, 10000);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "main-completed"), ==, 1);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "completed"), ==, 0);
    g_assert_cmphex(qtest_readl(q, RAM), ==, 0xa5a5a5a5);
    pipeline_input(q);
    pipeline_step(q, 10000);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "main-completed"), ==, 2);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "completed"), ==, 1);
    qtest_quit(q);
    unlink(bundle);
}

static void test_acquisition_boundary(void)
{
    for (unsigned bad = 0; bad < 2; bad++) {
        g_autofree char *bundle = NULL;
        QTestState *q = pipeline_start(&bundle, NULL);

        acquisition_configure(q);
        if (bad) {
            /* Invalid auxiliary transport cannot invalidate an eligible MAIN. */
            pipeline_configure(q, 0x80000000, 0x100, 0x200,
                               NIKON_READOUT_C170_FORMAT_PRIMARY);
        } else {
            /* Invalid MAIN geometry cannot discard a valid admitted auxiliary. */
            qtest_writel(q, CAPTURE + 0x1e7c, 257 << 16);
            qtest_writel(q, CAPTURE + 0x104, 4);
        }
        pipeline_request(q);
        pipeline_step(q, 10000);
        g_assert_cmpuint(property_at(q, PIPELINE_PATH, "main-completed"), ==, bad);
        g_assert_cmpuint(property_at(q, PIPELINE_PATH, "completed"), ==, !bad);
        g_assert_cmphex(qtest_readl(q, RAM), ==, bad ? 0xa5a5a5a5 : 0);
        qtest_quit(q);
        unlink(bundle);
    }
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
    pipeline_step(q, 10000);
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
    pipeline_step(q, 1000000);
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
    pipeline_step(q, 10000);
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
    pipeline_step(q, 10000);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "main-completed"), ==, 1);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "main-cancelled"), ==, 0);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "completed"), ==, 0);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "cancelled"), ==, 1);
    main_input_rise(q);
    pipeline_step(q, 10000);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "main-completed"), ==, 2);
    qtest_writel(q, CAPTURE + 0x210, 1);
    main_input_rise(q);
    /* Equal commits preserve work; staging alone does not change this frame. */
    qtest_writel(q, CAPTURE + 0x104, 5);
    qtest_writel(q, CAPTURE + 0x1e7c, 64 << 16);
    pipeline_step(q, 10000);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "main-completed"), ==, 3);
    qtest_writel(q, CAPTURE + 0x210, 1);
    main_input_rise(q);
    qtest_writel(q, CAPTURE + 0x100, 5);
    pipeline_step(q, 10000);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "main-completed"), ==, 3);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "main-cancelled"), ==, 1);

    for (unsigned i = 0; i < ARRAY_SIZE(units); i++) {
        main_input_rise(q);
        qtest_writel(q, CAPTURE + 0xc, units[i]);
        pipeline_step(q, 10000);
        g_assert_cmpuint(property_at(q, PIPELINE_PATH, "main-completed"), ==, 3);
        qtest_writel(q, CAPTURE + 8, units[i]);
    }
    main_input_rise(q);
    qtest_system_reset(q);
    pipeline_step(q, 10000);
    g_assert_cmpuint(property_at(q, "/machine/cmos", "frame-count"), ==, 0);
    g_assert_cmphex(qtest_readl(q, CAPTURE + 0x210) & 1, ==, 0);
    main_input_configure(q);
    qtest_set_irq_in(q, PIPELINE_PATH, "automatic-rise", 0, 1);
    pipeline_step(q, 10000);
    g_assert_cmpuint(property_at(q, "/machine/cmos", "frame-count"), ==, 0);
    main_input_rise(q);
    pipeline_step(q, 10000);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "main-completed"), ==, 4);
    /* Queue auxiliary behind main; its DMA starts only on the next fresh TG. */
    main_input_rise(q);
    pipeline_configure(q, RAM, 256, 1024, NIKON_READOUT_C170_FORMAT_PRIMARY);
    qtest_writel(q, CAPTURE + 0x10, 2);
    qtest_writel(q, CAPTURE + 8, 2);
    qtest_writel(q, READOUT_PARAMETERS + 0x400, 1);
    pipeline_step(q, 10000);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "main-completed"), ==, 5);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "completed"), ==, 0);
    pipeline_input(q);
    /* Main stop while auxiliary DMA is active must leave that DMA owned. */
    qtest_writel(q, CAPTURE + 0xc, 1);
    pipeline_step(q, 10000);
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
        pipeline_step(q, 10000);
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
    pipeline_step(q, 10000);
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
    /* Masking permits statistics ring progress without per-frame data ACKs;
     * MAIN acquisition has an independent lifetime.
     */
    qtest_writel(q, CAPTURE + 0x200, 0);
    qtest_writel(q, CAPTURE + 0x260, 0x80);
    for (unsigned i = 0; i < 3; i++) {
        main_input_rise(q);
        pipeline_step(q, 10000);
        statistics_rise(q);
        pipeline_step(q, 1000000);
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
    pipeline_step(q, 10000);
    statistics_rise(q);
    pipeline_step(q, 1000000);
    g_assert_cmpuint(property_at(q, "/machine/capture-statistics",
                                 "completed"), ==, 3);
    g_assert_cmpuint(property_at(q, "/machine/capture-statistics",
                                 "rejected"), ==, 1);
    g_assert_cmphex(qtest_readl(q, CAPTURE + 0x270), ==, 0);
    qtest_quit(q);
    unlink(bundle);
}

static void test_statistics_rgb(void)
{
    static const char header[] = "P6\n8 8\n255\n";
    static const uint8_t color[] = { 40, 80, 160 };
    g_autofree char *bundle = NULL;
    g_autofree char *scene = NULL;
    QTestState *q = pipeline_start(&bundle, NULL);
    uint8_t image[sizeof(header) - 1 + 8 * 8 * 3];
    int fd = g_file_open_tmp("nikon-statistics-rgb-XXXXXX", &scene, NULL);

    g_assert_cmpint(fd, >=, 0);
    close(fd);
    memcpy(image, header, sizeof(header) - 1);
    for (unsigned i = 0; i < 8 * 8; i++) {
        memcpy(image + sizeof(header) - 1 + 3 * i, color, 3);
    }
    g_assert_true(g_file_set_contents(scene, (const char *)image,
                                      sizeof(image), NULL));
    qtest_qmp_assert_success(q,
        "{'execute':'qom-set','arguments':{'path':'/machine/cmos',"
        "'property':'image','value':%s}}", scene);
    statistics_configure(q);
    statistics_input(q, NULL);
    pipeline_step(q, 1000000);
    g_assert_cmpuint(property_at(q, "/machine/capture-statistics",
                                 "completed"), ==, 1);
    for (unsigned c = 0; c < 3; c++) {
        uint32_t base = RAM + 0x101000 + (2 + c) * 0x10000;
        unsigned expected = (color[c] * 257U + 2) / 4;

        for (unsigned y = 0; y < 2; y++) {
            for (unsigned x = 0; x < 4; x++) {
                g_assert_cmpuint(qtest_readw(q, base + y * 16 + x * 2),
                                 ==, expected);
            }
            g_assert_cmphex(qtest_readl(q, base + y * 16 + 8), ==, 0);
        }
        g_assert_cmphex(qtest_readl(q, base - 4), ==, 0xa5a5a5a5);
        g_assert_cmphex(qtest_readl(q, base + 32), ==, 0xa5a5a5a5);
    }
    qtest_quit(q);
    unlink(scene);
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
    pipeline_step(q, 2000000);
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
    pipeline_step(q, 2000000);
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
                } else if (n >= 2 && n <= 4) {
                    g_assert_cmphex(qtest_readw(q, base + y * stride + x * 2),
                                    ==, MIN((mean + 2) / 4, 16383));
                } else if (n == 1) {
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
    pipeline_step(q, 2000000);
    g_assert_cmpuint(property_at(q, "/machine/capture-statistics",
                                 "completed"), ==, 1);
    g_assert_cmpuint(property_at(q, "/machine/capture-statistics",
                                 "blocked"), >, 0);
    qtest_writel(q, CAPTURE + 0x210, 0x40);
    statistics_rise(q);
    pipeline_step(q, 2000000);
    g_assert_cmpuint(property_at(q, "/machine/capture-statistics",
                                 "completed"), ==, 2);
    /* The newer A snapshot starts at slot1; the old job cannot advance it. */
    g_assert_cmphex(qtest_readl(q, RAM + 0x102000), ==, 0xa5a5a5a5);
    g_assert_cmphex(qtest_readl(q, RAM + 0x116000), ==, 0xa5a5a5a5);
    qtest_quit(q);
    unlink(bundle);
}

static void test_statistics_job_ownership(void)
{
    for (unsigned operation = 0; operation < 3; operation++) {
        g_autofree char *bundle = NULL;
        QTestState *q = pipeline_start(&bundle, NULL);
        uint8_t input[0x400], newer[0x400], expected[128], actual[128];
        uint32_t first = RAM + 0x101000, next = RAM + 0x300000;

        statistics_configure(q);
        for (unsigned n = 1; n < 10; n++) {
            qtest_writel(q, 0x2006a414 + n * 0x100, 0);
        }
        qtest_writel(q, CAPTURE + 0x100, 8);
        qtest_memset(q, next - 16, 0xa5, 160);
        statistics_input(q, input);
        /* Replace both the latest input and committed route before the owned
         * job's minimum deadline. No worker timing interception is needed.
         */
        qtest_writel(q, 0x2006a278, 0x102);
        qtest_writel(q, 0x2006a444, 0);
        qtest_writel(q, 0x2006a450, next);
        qtest_writel(q, CAPTURE + 0x100, 8);
        statistics_input(q, newer);
        g_assert_cmpint(memcmp(input, newer, sizeof(input)), !=, 0);
        if (operation == 1) {
            qtest_writel(q, CAPTURE + 0xc, 0x10);
        } else if (operation == 2) {
            qtest_system_reset(q);
        }
        pipeline_step(q, 2000000);
        if (operation) {
            g_assert_cmpuint(property_at(q, "/machine/capture-statistics",
                                         "completed"), ==, 0);
            if (operation == 1) {
                g_assert_cmpuint(property_at(q, "/machine/capture-statistics",
                                             "cancelled"), ==, 1);
            }
            g_assert_cmphex(qtest_readl(q, first), ==, 0xa5a5a5a5);
            g_assert_cmphex(qtest_readl(q, next), ==, 0xa5a5a5a5);
            g_assert_cmphex(qtest_readl(q, CAPTURE + 0x210) & 0x40, ==, 0);
            /* A fresh real input may succeed after cancellation; the old
             * generation cannot revive or produce an extra completion.
             */
            statistics_configure(q);
            statistics_input(q, newer);
            pipeline_step(q, 2000000);
            g_assert_cmpuint(property_at(q, "/machine/capture-statistics",
                                         "completed"), ==, 1);
            g_assert_cmpuint(qtest_readl(q, first + 12), ==,
                (statistics_sum(newer, 0, 0, 32, 2) + 32) / 64);
            g_assert_cmphex(qtest_readl(q, next), ==, 0xa5a5a5a5);
        } else {
            for (unsigned y = 0; y < 2; y++) {
                for (unsigned x = 0; x < 4; x++) {
                    uint64_t sum = statistics_sum(input, x * 32, y * 2, 32, 2);
                    int64_t difference =
                        (int64_t)statistics_sum(input, x * 32 + 31, y * 2, 1, 2) -
                        (int64_t)statistics_sum(input, x * 32, y * 2, 1, 2);
                    uint8_t *cell = expected + (y * 4 + x) * 16;

                    stl_le_p(cell, difference & 0xffffff);
                    stl_le_p(cell + 4, sum);
                    stl_le_p(cell + 8, 64);
                    stl_le_p(cell + 12, (sum + 32) / 64);
                }
            }
            qtest_memread(q, first, actual, sizeof(actual));
            g_assert_cmpmem(actual, sizeof(actual), expected, sizeof(expected));
            g_assert_cmphex(qtest_readl(q, next), ==, 0xa5a5a5a5);
            qtest_writel(q, CAPTURE + 0x210, 0x40);
            pipeline_step(q, 2000000);
            g_assert_cmpuint(property_at(q, "/machine/capture-statistics",
                                         "completed"), ==, 2);
            for (unsigned x = 0; x < 2; x++) {
                uint64_t sum = statistics_sum(newer, x * 64, 0, 64, 4);

                g_assert_cmpuint(qtest_readl(q, next + x * 16 + 8), ==, 256);
                g_assert_cmpuint(qtest_readl(q, next + x * 16 + 12), ==,
                                 (sum + 128) / 256);
            }
            g_assert_cmphex(qtest_readl(q, next - 4), ==, 0xa5a5a5a5);
            g_assert_cmphex(qtest_readl(q, next + 32), ==, 0xa5a5a5a5);
            qtest_memread(q, first, actual, sizeof(actual));
            g_assert_cmpmem(actual, sizeof(actual), expected, sizeof(expected));
        }
        qtest_quit(q);
        unlink(bundle);
    }
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
    pipeline_step(q, 100000);
    statistics_rise(q);
    pipeline_step(q, 2000000);
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
    pipeline_step(q, 2000000);
    statistics_input(q, NULL);
    statistics_rise(q);
    pipeline_step(q, 2000000);
    g_assert_cmpuint(property_at(q, "/machine/capture-statistics",
                                 "completed"), ==, 1);
    /* Empty original queues may discard a frame without any result reads. */
    qtest_writel(q, CAPTURE + 0x210, 0x40);
    statistics_rise(q);
    pipeline_step(q, 2000000);
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
    pipeline_step(q, 2000000);
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
    pipeline_step(q, 2000000);
    g_assert_cmpuint(property_at(q, "/machine/capture-statistics",
                                 "cancelled"), ==, 2);
    g_assert_cmphex(qtest_readl(q, CAPTURE + 0x210) & 0x40, ==, 0);
    qtest_writel(q, CAPTURE + 0xc, 0x40);
    statistics_input(q, NULL);
    statistics_rise(q);
    pipeline_step(q, 2000000);
    g_assert_cmpuint(property_at(q, "/machine/capture-statistics",
                                 "completed"), ==, 3);
    g_assert_cmphex(property_at(q, "/machine/capture-banks", "read-mask0"),
                    ==, 0xfff);
    g_assert_cmphex(property_at(q, "/machine/capture-banks", "read-mask1"),
                    ==, 0);
    qtest_qmp_assert_success(q, "{'execute':'system_reset'}");
    qtest_qmp_eventwait(q, "RESET");
    statistics_rise(q);
    pipeline_step(q, 2000000);
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
        pipeline_step(q, 2000000);
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
    pipeline_step(q, 10000);
    g_assert_cmphex(qtest_readl(q, RAM), ==, 0xa5a5a5a5);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "cancelled"), ==, 1);
    g_assert_cmpuint(property_at(q, READOUT_PATH, "completion-generation"),
                     ==, 0);

    pipeline_request(q);
    qtest_qmp_assert_success(q, "{'execute':'system_reset'}");
    qtest_qmp_eventwait(q, "RESET");
    pipeline_step(q, 10000);
    g_assert_cmphex(qtest_readl(q, RAM), ==, 0xa5a5a5a5);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "active-generation"), ==, 0);
    g_assert_cmpuint(property_at(q, READOUT_PATH, "request-generation"), ==, 0);
    g_assert_cmpuint(property_at(q, READOUT_PATH, "completion-generation"),
                     ==, 0);
    qtest_quit(q);
    g_assert_cmpint(unlink(bundle), ==, 0);
}

#define GROUP_PATH "/machine/capture-group"
#define GROUP_BASE 0x2006b800
#define GROUP_BUFFER (RAM + 0x180000)

static void group_rise(QTestState *q)
{
    qtest_set_irq_in(q, GROUP_PATH, "automatic-rise", 0, 0);
    qtest_set_irq_in(q, GROUP_PATH, "automatic-rise", 0, 1);
}

static void group_configure(QTestState *q)
{
    static const unsigned offsets[] = { 0x33c, 0x344, 0x348, 0x380, 0x480, 0x580,
                                        0x350, 0x450, 0x550, 0x360, 0x460, 0x560 };
    static const uint32_t cursor[] = {
        1, 2, 2, 0xc00, 0xc00, 0xc00,
        GROUP_BUFFER, GROUP_BUFFER + 0x80, GROUP_BUFFER + 0xd0,
        GROUP_BUFFER + 0x4000, GROUP_BUFFER + 0x4080, GROUP_BUFFER + 0x40d0,
    };

    qtest_qmp_assert_success(q,
        "{'execute':'qom-set','arguments':{'path':'/machine/cmos',"
        "'property':'image','value':%s}}", source);
    qtest_qmp_assert_success(q,
        "{'execute':'qom-set','arguments':{'path':'/machine/cmos',"
        "'property':'fixture-provider','value':false}}");
    qtest_writel(q, CAPTURE + 0x1d00, 8 << 16);
    qtest_writel(q, CAPTURE + 0x1d04, 8 << 16);
    qtest_writel(q, CAPTURE + 0x1e78, 8 << 16);
    qtest_writel(q, CAPTURE + 0x1e7c, 8 << 16);
    qtest_writel(q, CAPTURE + 0x1f00, 0x11);
    qtest_writel(q, GROUP_BASE, 1);
    qtest_writel(q, GROUP_BASE + 4, 1);
    qtest_writel(q, GROUP_BASE + 8, 2);
    qtest_writel(q, GROUP_BASE + 12, 2);
    qtest_writel(q, GROUP_BASE + 16, 4);
    qtest_writel(q, GROUP_BASE + 20, 4);
    qtest_writel(q, GROUP_BASE + 0x208, 8);
    qtest_writel(q, GROUP_BASE + 0x20c, 8);
    qtest_writel(q, GROUP_BASE + 0x22c, 3);
    qtest_writel(q, GROUP_BASE + 0x230, 1);
    for (unsigned i = 0; i < 2; i++) {
        qtest_writel(q, GROUP_BASE + 0x23c + i * 16, 8);
        qtest_writel(q, GROUP_BASE + 0x240 + i * 16, 8);
    }
    for (unsigned f = 0; f < 3; f++) {
        for (unsigned tap = 0; tap < 8; tap++) {
            qtest_writel(q, GROUP_BASE + 0x20 + f * 32 + tap * 4,
                         tap == 3 ? 1 : tap == 4 + f ? 127 : 0);
        }
    }
    for (unsigned i = 0; i < 65; i++) {
        qtest_writel(q, GROUP_BASE + 0x80 + i * 4, MIN(i * 1024, 65535));
    }
    for (unsigned i = 0; i < ARRAY_SIZE(cursor); i++) {
        qtest_writel(q, GROUP_BASE + offsets[i], cursor[i]);
    }
    qtest_memset(q, GROUP_BUFFER - 16, 0xa5, 0x7000);
    qtest_writel(q, CAPTURE + 0x10, 0x2d);
    qtest_writel(q, CAPTURE + 8, 0x2d);
    qtest_writel(q, CAPTURE + 0x100, 0x15);
}

static void group_input_complete(QTestState *q)
{
    uint64_t expected = property_at(q, PIPELINE_PATH, "main-completed") + 1;
    /* These acquisitions begin with one exposure nanosecond before readout. */
    int64_t start = qtest_clock_step(q, 1);

    /* Stop on actual MAIN DONE: a fixed 10 us step overshoots group admission. */
    while (property_at(q, PIPELINE_PATH, "main-completed") < expected) {
        int64_t now = qtest_clock_step_next(q);

        /* These fixtures use at most 1024 one-nanosecond CMOS row chunks. */
        g_assert_cmpint(now - start, <=, 10000);
    }
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "main-completed"), ==,
                     expected);
}

static void group_input(QTestState *q)
{
    qtest_writel(q, CAPTURE + 0x210, 1);
    main_input_rise(q);
    group_input_complete(q);
}

static void group_worker_drain(QTestState *q)
{
    int64_t deadline = g_get_monotonic_time() + 10 * G_TIME_SPAN_SECOND;

    /* Cancelled work releases its buffers without virtual publication. */
    while (property_at(q, GROUP_PATH, "inflight")) {
        g_assert_cmpint(g_get_monotonic_time(), <, deadline);
    }
}

static void group_large_input(QTestState *q)
{
    qtest_writel(q, CAPTURE + 0x1d00, 1024 << 16);
    qtest_writel(q, CAPTURE + 0x1d04, 1024 << 16);
    qtest_writel(q, CAPTURE + 0x1e78, 1024 << 16);
    qtest_writel(q, CAPTURE + 0x1e7c, 1024 << 16);
    qtest_writel(q, CAPTURE + 0x100, 0x15);
    group_input(q);
}

static void test_group_worker(void)
{
    g_autofree char *bundle = NULL;
    QTestState *q = pipeline_start(&bundle, NULL);
    uint32_t base = GROUP_BUFFER + 0x5800;

    group_configure(q);
    group_input(q);
    group_rise(q);
    pipeline_step(q, 999999);
    g_assert_cmphex(qtest_readl(q, base + 8), ==, 0xa5a5a5a5);
    g_assert_cmphex(qtest_readl(q, CAPTURE + 0x270), ==, 0);
    g_assert_cmpuint(property_at(q, GROUP_PATH, "completed"), ==, 0);
    /* Opaque destination writes made after admission survive actual DMA. */
    qtest_writew(q, base + 22, 0x1234);
    qtest_writew(q, base + 30, 0x5678);
    qtest_writew(q, base + 38, 0x9abc);
    qtest_writel(q, base + 0x94, 0x87654321);
    qtest_writew(q, base + 0xe6, 0x2468);
    /* Staging registers cannot replace the committed, owned configuration. */
    qtest_writel(q, GROUP_BASE + 0x80, 65535);
    pipeline_step(q, 1);
    g_assert_cmpuint(qtest_readl(q, base + 8), ==, 114864);
    g_assert_cmpuint(qtest_readl(q, base + 12), ==, 64);
    g_assert_cmpuint(qtest_readl(q, base + 0x80), ==, 114864);
    g_assert_cmpuint(qtest_readl(q, base + 0xdc), ==, 16);
    g_assert_cmphex(qtest_readw(q, base + 22), ==, 0x1234);
    g_assert_cmphex(qtest_readw(q, base + 30), ==, 0x5678);
    g_assert_cmphex(qtest_readw(q, base + 38), ==, 0x9abc);
    g_assert_cmphex(qtest_readl(q, base + 0x94), ==, 0x87654321);
    g_assert_cmphex(qtest_readw(q, base + 0xe6), ==, 0x2468);
    g_assert_cmphex(qtest_readl(q, CAPTURE + 0x270), ==, 1);
    g_assert_cmpuint(property_at(q, GROUP_PATH, "completed"), ==, 1);
    g_assert_cmpuint(property_at(q, GROUP_PATH, "inflight"), ==, 0);
    qtest_quit(q);
    unlink(bundle);
}

static void test_group_worker_cancel(void)
{
    for (unsigned control = 0; control < 3; control++) {
        g_autofree char *bundle = NULL;
        QTestState *q = pipeline_start(&bundle, NULL);
        uint32_t base = GROUP_BUFFER + 0x5800;
        uint64_t published;
        uint8_t retained[40], actual[40];

        group_configure(q);
        group_large_input(q);
        group_rise(q);
        /* Cancel after submitting arithmetic; publication may already have won. */
        qtest_clock_step(q, 1000000);
        if (control == 2) {
            qtest_system_reset(q);
        } else {
            qtest_writel(q, CAPTURE + (control ? 0x104 : 0xc),
                         control ? 0x10 : 0x20);
        }
        published = property_at(q, GROUP_PATH, "completed");
        g_assert_cmpuint(published, <=, 1);
        qtest_memread(q, base, retained, sizeof(retained));
        g_assert_cmphex((uint32_t)ldl_le_p(retained + 8), ==,
                       published ? 114864 : 0xa5a5a5a5);
        g_assert_cmphex((uint32_t)ldl_le_p(retained + 12), ==,
                       published ? 64 : 0xa5a5a5a5);
        group_worker_drain(q);
        pipeline_step(q, 2000000);
        qtest_memread(q, base, actual, sizeof(actual));
        g_assert_cmpmem(actual, sizeof(actual), retained, sizeof(retained));
        qtest_writel(q, CAPTURE + 0x270, 1);
        g_assert_cmphex(qtest_readl(q, CAPTURE + 0x270), ==, 0);
        g_assert_cmpuint(property_at(q, GROUP_PATH, "completed"), ==, published);
        g_assert_cmpuint(property_at(q, GROUP_PATH, "cancelled"), ==,
                         1 - published);

        group_configure(q);
        for (unsigned i = 0; i < 65; i++) {
            qtest_writel(q, GROUP_BASE + 0x80 + i * 4, MIN(i * 512, 32767));
        }
        qtest_writel(q, CAPTURE + 0x104, 0x10);
        group_rise(q);
        pipeline_step(q, 2000000);
        g_assert_cmphex(qtest_readl(q, base + 8), ==, 0xa5a5a5a5);
        group_input(q);
        pipeline_step(q, 1000000);
        g_assert_cmpuint(qtest_readl(q, base + 8), ==, 57424);
        g_assert_cmpuint(qtest_readl(q, base + 12), ==, 64);
        g_assert_cmpuint(qtest_readl(q, base + 0x80), ==, 57424);
        g_assert_cmpuint(qtest_readl(q, base + 0x138), ==, 2047 * 16);
        g_assert_cmphex(qtest_readl(q, CAPTURE + 0x270), ==, 1);
        qtest_writel(q, CAPTURE + 0x270, 1);
        pipeline_step(q, 2000000);
        g_assert_cmpuint(property_at(q, GROUP_PATH, "completed"), ==,
                         published + 1);
        g_assert_cmpuint(property_at(q, GROUP_PATH, "inflight"), ==, 0);
        qtest_quit(q);
        unlink(bundle);
    }
}

static void test_group_payload(void)
{
    g_autofree char *bundle = NULL;
    QTestState *q = pipeline_start(&bundle, NULL);
    uint32_t base = GROUP_BUFFER + 0x5800;
    uint8_t record[40];

    group_configure(q);
    qtest_irq_intercept_out_named(q, "/machine/capture-control", "sysbus-irq");
    qtest_writel(q, CAPTURE + 0x260, 1);
    group_rise(q); /* Metadata advances without input; no payload/completion. */
    pipeline_step(q, 1000000);
    g_assert_cmphex(qtest_readl(q, CAPTURE + 0x270), ==, 0);
    qtest_writel(q, CAPTURE + 0x104, 0x10); /* Restore initial ring cursor. */
    group_input(q);
    group_rise(q);
    g_assert_cmphex(qtest_readl(q, base + 8), ==, 0xa5a5a5a5);
    pipeline_step(q, 1000000);
    qtest_memread(q, base, record, sizeof(record));
    g_assert_cmpuint(ldl_le_p(record + 8), ==, 114864);
    g_assert_cmpuint(ldl_le_p(record + 12), ==, 64);
    g_assert_cmpuint(ldl_le_p(record + 16), ==, 6132);
    g_assert_cmpuint(ldl_le_p(record + 24), ==, 12264);
    g_assert_cmpuint(ldl_le_p(record + 32), ==, 18396);
    g_assert_cmpuint(ldl_le_p(record) & 0xfff, ==, 287);
    g_assert_cmpuint(((uint32_t)ldl_le_p(record) >> 12) |
                     ((ldl_le_p(record + 4) & 7) << 20), ==, 114864);
    g_assert_cmpuint((uint32_t)ldl_le_p(record + 4) >> 3, ==,
                     16u * (1028u * 1028 + 2056u * 2056 + 4095u * 4095));
    g_assert_cmpuint(lduw_le_p(record + 20), ==, 1794);
    g_assert_cmphex(lduw_le_p(record + 22), ==, 0xa5a5);
    g_assert_cmphex(lduw_le_p(record + 30), ==, 0xa5a5);
    g_assert_cmphex(lduw_le_p(record + 38), ==, 0xa5a5);
    g_assert_cmpuint(qtest_readl(q, base + 0x80), ==, 114864);
    g_assert_cmpuint(qtest_readl(q, base + 0x84), ==, 64);
    g_assert_cmpuint(qtest_readl(q, base + 0x88), ==, 6132);
    g_assert_cmphex(qtest_readl(q, base + 0x94), ==, 0xa5a5a5a5);
    for (unsigned i = 0; i < 4; i++) {
        static const unsigned mean[] = { 0, 1028, 2056, 4095 };

        g_assert_cmpuint(qtest_readl(q, base + 0xd8 + i * 32), ==, mean[i] * 16);
        g_assert_cmpuint(qtest_readl(q, base + 0xdc + i * 32), ==, 16);
    }
    g_assert_cmphex(qtest_readl(q, base - 4), ==, 0xa5a5a5a5);
    g_assert_cmphex(qtest_readl(q, base + 0x150), ==, 0xa5a5a5a5);
    g_assert_cmphex(qtest_readl(q, GROUP_BUFFER), ==, 0xa5a5a5a5);
    g_assert_cmphex(qtest_readl(q, CAPTURE + 0x270), ==, 1);
    g_assert_true(qtest_get_irq(q, NIKON_CAPTURE_THIRD_EVENT));
    g_assert_cmpuint(property_at(q, GROUP_PATH, "completed"), ==, 1);
    /* Even masked pending work owns its output until the matching ACK. */
    qtest_writel(q, CAPTURE + 0x260, 0);
    group_input(q);
    group_rise(q);
    pipeline_step(q, 1000000);
    g_assert_cmpuint(property_at(q, GROUP_PATH, "completed"), ==, 1);
    qtest_writel(q, CAPTURE + 0x270, 2);
    group_rise(q);
    pipeline_step(q, 1000000);
    g_assert_cmpuint(property_at(q, GROUP_PATH, "completed"), ==, 1);
    qtest_writel(q, CAPTURE + 0x270, 1);
    group_rise(q);
    pipeline_step(q, 1000000);
    g_assert_cmpuint(property_at(q, GROUP_PATH, "completed"), ==, 2);
    qtest_writel(q, CAPTURE + 0x270, 1);
    group_rise(q);
    pipeline_step(q, 1000000);
    g_assert_cmpuint(property_at(q, GROUP_PATH, "completed"), ==, 2);
    qtest_quit(q);
    unlink(bundle);
}

static void test_group_ready(void)
{
    g_autofree char *bundle = NULL;
    QTestState *q = pipeline_start(&bundle, NULL);
    uint32_t base = GROUP_BUFFER + 0x4000;

    group_configure(q);
    group_rise(q); /* Initial unit2; acquisition has not completed. */
    qtest_writel(q, CAPTURE + 0x210, 1);
    main_input_rise(q);
    group_rise(q); /* Delayed acquisition crosses TG: admission uses unit0. */
    g_assert_cmpuint(property_at(q, GROUP_PATH, "completed"), ==, 0);
    pipeline_step(q, 1010000);
    /* No further TG is needed to calculate and DMA the completed input. */
    g_assert_cmpuint(qtest_readl(q, base + 8), ==, 114864);
    g_assert_cmpuint(qtest_readl(q, base + 12), ==, 64);
    g_assert_cmphex(qtest_readl(q, GROUP_BASE + 0x370), ==, 1);
    g_assert_cmphex(qtest_readl(q, GROUP_BASE + 0x374), ==, 0);
    g_assert_cmphex(qtest_readl(q, GROUP_BASE + 0x378), ==, GROUP_BUFFER);
    g_assert_cmphex(qtest_readl(q, GROUP_BASE + 0x478), ==, GROUP_BUFFER + 0x80);
    g_assert_cmphex(qtest_readl(q, GROUP_BASE + 0x578), ==, GROUP_BUFFER + 0xd0);
    g_assert_cmphex(qtest_readl(q, GROUP_BASE + 0x37c), ==, base);
    g_assert_cmphex(qtest_readl(q, GROUP_BASE + 0x47c), ==, base + 0x80);
    g_assert_cmphex(qtest_readl(q, GROUP_BASE + 0x57c), ==, base + 0xd0);
    g_assert_cmphex(qtest_readl(q, GROUP_BUFFER + 0x5808), ==, 0xa5a5a5a5);
    g_assert_cmphex(qtest_readl(q, CAPTURE + 0x270), ==, 1);
    g_assert_cmpuint(property_at(q, GROUP_PATH, "completed"), ==, 1);
    qtest_quit(q);
    unlink(bundle);
}

static void test_group_commit_input(void)
{
    for (unsigned overlap = 0; overlap < 2; overlap++) {
        g_autofree char *bundle = NULL;
        QTestState *q = pipeline_start(&bundle, NULL);
        uint32_t first = GROUP_BUFFER + 0xc00;
        uint32_t second = GROUP_BUFFER + 0x1800;

        group_configure(q);
        group_rise(q);
        group_input(q);
        pipeline_step(q, 1000000);
        g_assert_cmpuint(qtest_readl(q, GROUP_BUFFER + 0x5808), ==, 114864);
        qtest_writel(q, CAPTURE + 0x270, 1);
        qtest_writel(q, GROUP_BASE + 0x33c, 0);
        qtest_writel(q, GROUP_BASE + 0x344, 1);
        qtest_writel(q, CAPTURE + 0x104, 0x10);
        pipeline_step(q, 2000000);
        /* A commit cannot replay the already delivered MAIN generation. */
        g_assert_cmphex(qtest_readl(q, first + 8), ==, 0xa5a5a5a5);
        g_assert_cmphex(qtest_readl(q, CAPTURE + 0x270), ==, 0);
        g_assert_cmpuint(property_at(q, GROUP_PATH, "completed"), ==, 1);

        main_input_rise(q);
        /* Commit while actual MAIN acquisition is in flight, without group TG. */
        qtest_writel(q, CAPTURE + 0x104, 0x10);
        group_input_complete(q);
        g_assert_cmphex(qtest_readl(q, GROUP_BASE + 0x370), ==, 1);
        g_assert_cmphex(qtest_readl(q, GROUP_BASE + 0x374), ==, 2);
        g_assert_cmphex(qtest_readl(q, first + 8), ==, 0xa5a5a5a5);
        if (overlap) {
            /* A real TG during owned work advances metadata, not the job. */
            group_rise(q);
        }
        pipeline_step(q, 999999);
        g_assert_cmphex(qtest_readl(q, first + 8), ==, 0xa5a5a5a5);
        pipeline_step(q, 1);
        /* RED: a fresh completion after commit needs no later group TG. */
        g_assert_cmpuint(qtest_readl(q, first + 8), ==, 114864);
        g_assert_cmpuint(qtest_readl(q, first + 12), ==, 64);
        g_assert_cmpuint(qtest_readl(q, first + 0x80), ==, 114864);
        g_assert_cmpuint(qtest_readl(q, first + 0xdc), ==, 16);
        g_assert_cmphex(qtest_readl(q, GROUP_BASE + 0x370), ==, 0);
        g_assert_cmphex(qtest_readl(q, GROUP_BASE + 0x374), ==, 1);
        g_assert_cmphex(qtest_readl(q, GROUP_BASE + 0x378), ==, first);
        g_assert_cmphex(qtest_readl(q, GROUP_BASE + 0x37c), ==,
                       GROUP_BUFFER + 0x4c00);
        g_assert_cmphex(qtest_readl(q, CAPTURE + 0x270), ==, 1);

        group_input(q);
        pipeline_step(q, 1000000);
        g_assert_cmphex(qtest_readl(q, second + 8), ==, 0xa5a5a5a5);
        g_assert_cmpuint(property_at(q, GROUP_PATH, "completed"), ==, 2);
        if (!overlap) {
            /* The first actual TG advances the initial packet exactly once. */
            group_rise(q);
        }
        qtest_writel(q, CAPTURE + 0x270, 1);
        pipeline_step(q, 999999);
        g_assert_cmphex(qtest_readl(q, second + 8), ==, 0xa5a5a5a5);
        pipeline_step(q, 1);
        /* Capacity retry uses current cursor2, even after job1 latched results. */
        g_assert_cmpuint(qtest_readl(q, second + 8), ==, 114864);
        g_assert_cmpuint(qtest_readl(q, second + 12), ==, 64);
        g_assert_cmphex(qtest_readl(q, GROUP_BASE + 0x374), ==, 2);
        g_assert_cmpuint(property_at(q, GROUP_PATH, "completed"), ==, 3);
        qtest_writel(q, CAPTURE + 0x270, 1);
        pipeline_step(q, 2000000);
        g_assert_cmpuint(property_at(q, GROUP_PATH, "completed"), ==, 3);
        group_rise(q);
        g_assert_cmphex(qtest_readl(q, GROUP_BASE + 0x374), ==, 0);
        group_input(q);
        pipeline_step(q, 1000000);
        g_assert_cmpuint(qtest_readl(q, GROUP_BUFFER + 8), ==, 114864);
        g_assert_cmpuint(qtest_readl(q, GROUP_BUFFER + 12), ==, 64);
        g_assert_cmpuint(property_at(q, GROUP_PATH, "completed"), ==, 4);
        qtest_quit(q);
        unlink(bundle);
    }
}

static void test_group_boundaries(void)
{
    static const uint32_t bad[][2] = {
        { 0x80, 65535 }, { 0x20c, 1 }, { 0x23c, 9 }, { 0x298, 1 },
        { 0x580, 32 }, { 0x360, 0x80000000 }, { 0x450, GROUP_BUFFER },
        { 0x230, 0 }, { 0x80 + 16 * 4, 0 },
    };

    for (unsigned i = 0; i < ARRAY_SIZE(bad); i++) {
        g_autofree char *bundle = NULL;
        QTestState *q = pipeline_start(&bundle, NULL);

        group_configure(q);
        qtest_writel(q, GROUP_BASE + bad[i][0], bad[i][1]);
        if (!i) {
            for (unsigned point = 0; point < 65; point++) {
                qtest_writel(q, GROUP_BASE + 0x80 + point * 4, 65535);
            }
        }
        qtest_writel(q, CAPTURE + 0x104, 0x10);
        group_input(q);
        group_rise(q);
        pipeline_step(q, 1000000);
        g_assert_cmpuint(property_at(q, GROUP_PATH, "completed"), ==, 0);
        g_assert_cmpuint(property_at(q, GROUP_PATH, "rejected"), ==, 1);
        g_assert_cmphex(qtest_readl(q, CAPTURE + 0x270), ==, 0);
        g_assert_cmphex(qtest_readl(q, GROUP_BUFFER + 0x5808), ==, 0xa5a5a5a5);
        qtest_quit(q);
        unlink(bundle);
    }
}

static void test_group_cancellation(void)
{
    static const unsigned controls[] = { 0xc, 0x14, 0, 0x104 };

    for (unsigned i = 0; i < 5; i++) {
        g_autofree char *bundle = NULL;
        QTestState *q = pipeline_start(&bundle, NULL);

        group_configure(q);
        group_input(q);
        /* Fresh input owns its initial job before TG; controls must cancel it. */
        if (i == 4) {
            qtest_system_reset(q);
        } else {
            qtest_writel(q, CAPTURE + controls[i], i == 3 ? 0x10 : 0x20);
        }
        pipeline_step(q, 1000000);
        g_assert_cmpuint(property_at(q, GROUP_PATH, "completed"), ==, 0);
        g_assert_cmpuint(property_at(q, GROUP_PATH, "cancelled"), ==, 1);
        g_assert_cmphex(qtest_readl(q, CAPTURE + 0x270), ==, 0);
        g_assert_cmphex(qtest_readl(q, GROUP_BUFFER + 0x5808), ==, 0xa5a5a5a5);
        qtest_quit(q);
        unlink(bundle);
    }
}

static void test_group_families(void)
{
    for (unsigned families = 0; families < 3; families++) {
        g_autofree char *bundle = NULL;
        QTestState *q = pipeline_start(&bundle, NULL);
        uint32_t base = GROUP_BUFFER + 0x5800;

        group_configure(q);
        qtest_writel(q, GROUP_BASE + 0x22c, families);
        /* Inactive family destinations are deliberately outside RAM. */
        for (unsigned bank = 0; bank < 2; bank++) {
            if (!(families & 1)) {
                qtest_writel(q, GROUP_BASE + 0x350 + bank * 16, 0x80000000);
                qtest_writel(q, GROUP_BASE + 0x450 + bank * 16, 0x80000000);
            }
            if (!(families & 2)) {
                qtest_writel(q, GROUP_BASE + 0x550 + bank * 16, 0x80000000);
            }
        }
        qtest_writel(q, CAPTURE + 0x104, 0x10);
        group_input(q);
        group_rise(q);
        pipeline_step(q, 1000000);
        g_assert_cmphex(qtest_readl(q, base + 8), ==,
                       families & 1 ? 114864 : 0xa5a5a5a5);
        g_assert_cmphex(qtest_readl(q, base + 0x80), ==,
                       families & 1 ? 114864 : 0xa5a5a5a5);
        g_assert_cmphex(qtest_readl(q, base + 0xd8), ==,
                       families & 2 ? 0 : 0xa5a5a5a5);
        g_assert_cmphex(qtest_readl(q, CAPTURE + 0x270), ==, !!families);
        g_assert_cmpuint(property_at(q, GROUP_PATH, "completed"), ==, !!families);
        g_assert_cmpuint(property_at(q, GROUP_PATH, "rejected"), ==, 0);
        qtest_quit(q);
        unlink(bundle);
    }
}

#define WORK_PATH "/machine/capture-processing"
#define WORK_BUFFER (RAM + 0x100000)

static void work_rise(QTestState *q)
{
    qtest_set_irq_in(q, WORK_PATH, "input-ready", 0, 0);
    qtest_set_irq_in(q, WORK_PATH, "input-ready", 0, 1);
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
    main_input_rise(q);
    pipeline_step(q, 10000);
    qtest_writel(q, CAPTURE + 0x210, qtest_readl(q, CAPTURE + 0x210));
}

static void work_ack(QTestState *q)
{
    qtest_writel(q, CAPTURE + 0x210, qtest_readl(q, CAPTURE + 0x210));
    qtest_writel(q, CAPTURE + 0x290, qtest_readl(q, CAPTURE + 0x290));
}

static void test_front_capacity(void)
{
    for (unsigned cancellation = 0; cancellation < 3; cancellation++) {
        g_autofree char *bundle = NULL;
        QTestState *q = pipeline_start(&bundle, NULL);
        uint8_t expected[32], actual[32];

        work_configure(q, 2, true);
        work_input(q);
        pipeline_step(q, 2000000);
        qtest_memread(q, WORK_BUFFER + 0x1000, expected, sizeof(expected));
        g_assert_cmphex(ldl_le_p(expected), ==, 0);
        g_assert_cmphex(qtest_readl(q, CAPTURE + 0x210) & 0x18, ==, 0x18);
        g_assert_cmphex(qtest_readl(q, CAPTURE + 0x290) & 0x80, ==, 0x80);
        main_input_rise(q);
        pipeline_step(q, 10000); /* A fresh MAIN is blocked by the old packet. */
        qtest_writel(q, CAPTURE + 0x210, 0);
        qtest_writel(q, CAPTURE + 0x210, 0x10000000);
        qtest_writel(q, CAPTURE + 0x210, 8);
        pipeline_step(q, 2000000);
        g_assert_cmphex(qtest_readl(q, WORK_BUFFER), ==, 0xa5a5a5a5);
        qtest_writel(q, CAPTURE + 0x210, 0x10);
        pipeline_step(q, 2000000);
        g_assert_cmphex(qtest_readl(q, WORK_BUFFER), ==, 0xa5a5a5a5);
        if (cancellation == 1) {
            qtest_writel(q, CAPTURE + 0xc, 0x200);
        } else if (cancellation == 2) {
            qtest_system_reset(q);
        }
        qtest_writel(q, CAPTURE + 0x290, 0x80); /* Aggregate capacity restored. */
        pipeline_step(q, 999999);
        g_assert_cmphex(qtest_readl(q, WORK_BUFFER), ==, 0xa5a5a5a5);
        pipeline_step(q, 1);
        if (!cancellation) {
            /* RED: no TG or new input arrives between ACK and this real DMA. */
            qtest_memread(q, WORK_BUFFER, actual, sizeof(actual));
            g_assert_cmpmem(actual, sizeof(actual), expected, sizeof(expected));
            g_assert_cmphex(qtest_readl(q, CAPTURE + 0x210) & 0x18, ==, 0x18);
            g_assert_cmphex(qtest_readl(q, CAPTURE + 0x290) & 0x80, ==, 0x80);
            qtest_memset(q, WORK_BUFFER + 0x1000, 0xa5, sizeof(actual));
            work_ack(q); /* No new generation: freeing capacity is no result. */
            pipeline_step(q, 2000000);
            g_assert_cmphex(qtest_readl(q, WORK_BUFFER + 0x1000), ==, 0xa5a5a5a5);
        } else {
            g_assert_cmphex(qtest_readl(q, WORK_BUFFER), ==, 0xa5a5a5a5);
        }
        g_assert_cmpuint(property_at(q, WORK_PATH, "completed"), ==,
                         cancellation ? 1 : 2);
        qtest_quit(q);
        unlink(bundle);
    }
}

static void test_capture_domains(void)
{
    g_autofree char *bundle = NULL;
    QTestState *q = pipeline_start(&bundle, NULL);
    uint8_t raw[32], actual[128], statistics[10][128];
    const uint32_t statistics_base = RAM + 0x600000;

    work_configure(q, 2, true);
    statistics_configure(q);
    /* Independent contracts require disjoint output storage, including banks. */
    for (unsigned n = 0; n < 10; n++) {
        unsigned addresses = (n >= 2 && n <= 4) || n == 9 ? 4 : 1;
        uint32_t port = 0x2006a400 + n * 0x100;
        uint32_t base = statistics_base + n * 0x10000;

        for (unsigned bank = 0; bank < 2; bank++) {
            for (unsigned a = 0; a < addresses; a++) {
                qtest_writel(q, port + 0x50 + bank * 16 + a * 4, base);
            }
        }
        qtest_memset(q, base, 0xa5, 0x3000);
    }
    qtest_writel(q, CAPTURE + 0x100, 8);
    main_input_rise(q);
    pipeline_step(q, 10000);
    pipeline_step(q, 2000000);
    qtest_memread(q, WORK_BUFFER + 0x1000, raw, sizeof(raw));
    g_assert_cmphex((uint32_t)ldl_le_p(raw), ==, 0);
    g_assert_cmpuint(qtest_readl(q, statistics_base + 0x1008), ==, 2);
    g_assert_cmpuint(qtest_readl(q, statistics_base + 0x100c), ==, 0);
    g_assert_cmphex(qtest_readl(q, CAPTURE + 0x210) & 0x58, ==, 0x58);
    g_assert_cmphex(qtest_readl(q, CAPTURE + 0x290) & 0x80, ==, 0x80);
    for (unsigned n = 0; n < 10; n++) {
        qtest_memread(q, statistics_base + n * 0x10000 + 0x1000,
                      statistics[n], sizeof(statistics[n]));
    }

    /* ACK only RAW's packet; statistics retains its actual data and raw0.bit6. */
    qtest_writel(q, CAPTURE + 0x210, 0x18);
    qtest_writel(q, CAPTURE + 0x290, 0x80);
    main_input_rise(q);
    pipeline_step(q, 10000);
    pipeline_step(q, 2000000);
    qtest_memread(q, WORK_BUFFER, actual, sizeof(raw));
    /* RED: the old cross-domain gate leaves this real RAW DMA at its sentinel. */
    g_assert_cmpmem(actual, sizeof(raw), raw, sizeof(raw));
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "main-completed"), ==, 2);
    g_assert_cmpuint(property_at(q, WORK_PATH, "completed"), ==, 2);
    g_assert_cmpuint(property_at(q, "/machine/capture-statistics", "completed"),
                     ==, 1);
    for (unsigned n = 0; n < 10; n++) {
        qtest_memread(q, statistics_base + n * 0x10000 + 0x1000,
                      actual, sizeof(actual));
        g_assert_cmpmem(actual, sizeof(actual), statistics[n], sizeof(actual));
    }

    qtest_memset(q, WORK_BUFFER + 0x1000, 0xa5, sizeof(raw));
    main_input_rise(q);
    pipeline_step(q, 10000);
    pipeline_step(q, 2000000);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "main-completed"), ==, 3);
    g_assert_cmphex(qtest_readl(q, WORK_BUFFER + 0x1000), ==, 0xa5a5a5a5);
    /* RAW's own complete aggregate still blocks, independently of statistics. */
    qtest_writel(q, CAPTURE + 0x210, 0);
    qtest_writel(q, CAPTURE + 0x210, 0x10000000);
    qtest_writel(q, CAPTURE + 0x210, 8);
    pipeline_step(q, 2000000);
    g_assert_cmphex(qtest_readl(q, WORK_BUFFER + 0x1000), ==, 0xa5a5a5a5);
    qtest_writel(q, CAPTURE + 0x210, 0x10);
    pipeline_step(q, 2000000);
    g_assert_cmphex(qtest_readl(q, WORK_BUFFER + 0x1000), ==, 0xa5a5a5a5);
    qtest_writel(q, CAPTURE + 0x290, 0x80);
    pipeline_step(q, 999999);
    g_assert_cmphex(qtest_readl(q, WORK_BUFFER + 0x1000), ==, 0xa5a5a5a5);
    pipeline_step(q, 1);
    qtest_memread(q, WORK_BUFFER + 0x1000, actual, sizeof(raw));
    g_assert_cmpmem(actual, sizeof(raw), raw, sizeof(raw));
    g_assert_cmphex(qtest_readl(q, CAPTURE + 0x210) & 0x58, ==, 0x58);
    g_assert_cmpuint(property_at(q, WORK_PATH, "completed"), ==, 3);
    g_assert_cmpuint(property_at(q, "/machine/capture-statistics", "completed"),
                     ==, 1);
    for (unsigned n = 0; n < 10; n++) {
        qtest_memread(q, statistics_base + n * 0x10000 + 0x1000,
                      actual, sizeof(actual));
        g_assert_cmpmem(actual, sizeof(actual), statistics[n], sizeof(actual));
    }

    /* Only its own ACK permits statistics to consume the latest MAIN snapshot. */
    qtest_writel(q, CAPTURE + 0x210, 0x40);
    pipeline_step(q, 999999);
    g_assert_cmphex(qtest_readl(q, statistics_base + 0x2008), ==, 0xa5a5a5a5);
    pipeline_step(q, 1);
    g_assert_cmpuint(qtest_readl(q, statistics_base + 0x2008), ==, 2);
    g_assert_cmpuint(qtest_readl(q, statistics_base + 0x200c), ==, 0);
    g_assert_cmphex(qtest_readl(q, CAPTURE + 0x210) & 0x58, ==, 0x58);
    g_assert_cmpuint(property_at(q, "/machine/capture-statistics", "completed"),
                     ==, 2);
    qtest_writel(q, CAPTURE + 0x210, 0x40);
    pipeline_step(q, 2000000);
    g_assert_cmphex(qtest_readl(q, statistics_base + 8), ==, 0xa5a5a5a5);
    g_assert_cmpuint(property_at(q, "/machine/capture-statistics", "completed"),
                     ==, 2);
    qtest_quit(q);
    unlink(bundle);
}

static void test_statistics_capacity(void)
{
    for (unsigned cancellation = 0; cancellation < 3; cancellation++) {
        g_autofree char *bundle = NULL;
        QTestState *q = pipeline_start(&bundle, NULL);
        uint8_t input[0x400];
        uint32_t target = RAM + 0x102000;

        statistics_configure(q);
        statistics_input(q, NULL);
        pipeline_step(q, 2000000);
        statistics_input(q, input);
        qtest_writel(q, CAPTURE + 0x210, 0);
        qtest_writel(q, CAPTURE + 0x210, 8);
        qtest_writel(q, CAPTURE + 0x270, 0x20); /* Stage ACK is not data capacity. */
        pipeline_step(q, 2000000);
        g_assert_cmphex(qtest_readl(q, target), ==, 0xa5a5a5a5);
        if (cancellation == 1) {
            qtest_writel(q, CAPTURE + 0xc, 0x10);
        } else if (cancellation == 2) {
            qtest_system_reset(q);
        }
        qtest_writel(q, CAPTURE + 0x210, 0x40);
        pipeline_step(q, 999999);
        g_assert_cmphex(qtest_readl(q, target), ==, 0xa5a5a5a5);
        pipeline_step(q, 1);
        if (!cancellation) {
            g_assert_cmpuint(qtest_readl(q, target + 8), ==, 64);
            g_assert_cmpuint(qtest_readl(q, target + 12), ==,
                             (statistics_sum(input, 0, 0, 32, 2) + 32) / 64);
            g_assert_cmphex(qtest_readl(q, target + 128), ==, 0xa5a5a5a5);
            g_assert_cmphex(qtest_readl(q, CAPTURE + 0x210) & 0x40, ==, 0x40);
            qtest_writel(q, CAPTURE + 0x210, 0x40);
            pipeline_step(q, 2000000);
            g_assert_cmphex(qtest_readl(q, RAM + 0x100000), ==, 0xa5a5a5a5);
        } else {
            g_assert_cmphex(qtest_readl(q, target), ==, 0xa5a5a5a5);
        }
        g_assert_cmpuint(property_at(q, "/machine/capture-statistics", "completed"),
                         ==, cancellation ? 1 : 2);
        qtest_quit(q);
        unlink(bundle);
    }
}

static void test_group_capacity(void)
{
    for (unsigned cancellation = 0; cancellation < 3; cancellation++) {
        g_autofree char *bundle = NULL;
        QTestState *q = pipeline_start(&bundle, NULL);
        uint8_t expected[40], actual[40];
        uint32_t target = GROUP_BUFFER + 0x4000;

        group_configure(q);
        group_rise(q);
        group_input(q);
        pipeline_step(q, 2000000);
        qtest_memread(q, GROUP_BUFFER + 0x5800, expected, sizeof(expected));
        g_assert_cmpuint(ldl_le_p(expected + 8), ==, 114864);
        group_input(q);
        group_rise(q); /* Select next metadata cursor while data is blocked. */
        g_assert_cmpuint(qtest_readl(q, GROUP_BASE + 0x374), ==, 0);
        qtest_writel(q, CAPTURE + 0x270, 0);
        qtest_writel(q, CAPTURE + 0x270, 2);
        qtest_writel(q, CAPTURE + 0x290, 0x80);
        pipeline_step(q, 2000000);
        g_assert_cmphex(qtest_readl(q, target + 8), ==, 0xa5a5a5a5);
        if (cancellation == 1) {
            qtest_writel(q, CAPTURE + 0xc, 0x20);
        } else if (cancellation == 2) {
            qtest_system_reset(q);
        }
        qtest_writel(q, CAPTURE + 0x270, 1);
        g_assert_cmpuint(qtest_readl(q, GROUP_BASE + 0x374), ==, 0);
        pipeline_step(q, 999999);
        g_assert_cmphex(qtest_readl(q, target + 8), ==, 0xa5a5a5a5);
        pipeline_step(q, 1);
        if (!cancellation) {
            qtest_memread(q, target, actual, sizeof(actual));
            g_assert_cmpmem(actual, sizeof(actual), expected, sizeof(expected));
            g_assert_cmpuint(qtest_readl(q, target + 0x80), ==, 114864);
            g_assert_cmpuint(qtest_readl(q, target + 0xdc), ==, 16);
            g_assert_cmpuint(qtest_readl(q, GROUP_BASE + 0x374), ==, 0);
            qtest_writel(q, CAPTURE + 0x270, 1);
            pipeline_step(q, 2000000);
            g_assert_cmphex(qtest_readl(q, GROUP_BUFFER + 0x4c08), ==, 0xa5a5a5a5);
        } else {
            g_assert_cmphex(qtest_readl(q, target + 8), ==, 0xa5a5a5a5);
        }
        g_assert_cmpuint(property_at(q, GROUP_PATH, "completed"), ==,
                         cancellation ? 1 : 2);
        qtest_quit(q);
        unlink(bundle);
    }
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
    pipeline_step(q, 2000000);
    g_assert_cmpuint(property_at(q, WORK_PATH, "completed"), ==, 0);
    work_input(q);
    work_rise(q);
    /* Neither live RAM reuse nor uncommitted parameters change the job. */
    qtest_memset(q, RAM, 0x55, 0x400);
    qtest_writel(q, 0x20069a54, WORK_BUFFER + 0x3000);
    qtest_writel(q, 0x20069600, 0xffffffff);
    pipeline_step(q, 2000000);
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
    pipeline_step(q, 2000000);
    g_assert_cmpuint(property_at(q, WORK_PATH, "completed"), ==, 1);
    qtest_quit(q);
    unlink(bundle);
}

static void test_capture_work_snapshot(void)
{
    static const uint8_t white[] = {
        'P', '5', '\n', '2', ' ', '2', '\n', '2', '5', '5', '\n',
        255, 255, 255, 255,
    };
    static const uint8_t expected[2][12] = {
        { 0, 0, 0, 0, 0, 0, 0xc0, 0, 0xc, 0xc0, 0, 0xc },
        { 0x80, 1, 0x18, 0x80, 1, 0x18, 0xfd, 0xd2, 0x2f,
          0xfd, 0xd2, 0x2f },
    };
    static const uint8_t packed_white[12] = {
        0xfd, 0xd2, 0x2f, 0xfd, 0xd2, 0x2f,
        0xfd, 0xd2, 0x2f, 0xfd, 0xd2, 0x2f,
    };
    g_autofree char *bundle = NULL;
    g_autofree char *path = NULL;
    QTestState *q = pipeline_start(&bundle, NULL);
    uint8_t row[16];
    int fd = g_file_open_tmp("nikonrawwhiteXXXXXX", &path, NULL);

    g_assert_cmpint(fd, >=, 0);
    close(fd);
    g_assert_true(g_file_set_contents(path, (const char *)white,
                                      sizeof(white), NULL));
    work_configure(q, 2, true);
    main_input_rise(q);
    pipeline_step(q, 3); /* One exposure tick and two complete MAIN rows. */
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "main-completed"), ==, 1);
    qtest_qmp_assert_success(q,
        "{'execute':'qom-set','arguments':{'path':'/machine/cmos',"
        "'property':'image','value':%s}}", path);
    main_input_rise(q);
    pipeline_step(q, 3); /* Replaces pipeline MAIN bytes while job1 owns input. */
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "main-completed"), ==, 2);
    qtest_writel(q, 0x20069a54, WORK_BUFFER + 0x3000);
    qtest_writel(q, 0x20069600, 0xffffffff); /* Uncommitted staging only. */
    qtest_writel(q, WORK_BUFFER + 0x100c, 0x12345678);
    qtest_writel(q, WORK_BUFFER + 0x101c, 0x87654321);
    pipeline_step(q, 999996); /* Exactly admission + 999999 ns. */
    g_assert_cmphex(qtest_readl(q, WORK_BUFFER + 0x1000), ==, 0xa5a5a5a5);
    g_assert_cmphex(qtest_readl(q, CAPTURE + 0x210) & 0x18, ==, 0);
    pipeline_step(q, 1);
    for (unsigned y = 0; y < 2; y++) {
        qtest_memread(q, WORK_BUFFER + 0x1000 + 16 * y, row, sizeof(row));
        g_assert_cmpmem(row, 12, expected[y], 12);
        g_assert_cmphex((uint32_t)ldl_le_p(row + 12), ==,
                       y ? 0x87654321 : 0x12345678);
    }
    g_assert_cmphex(qtest_readl(q, WORK_BUFFER + 0x1020), ==, 0xa5a5a5a5);
    g_assert_cmphex(qtest_readl(q, CAPTURE + 0x210) & 0x18, ==, 0x18);
    g_assert_cmphex(qtest_readl(q, CAPTURE + 0x290) & 0x80, ==, 0x80);
    qtest_writel(q, CAPTURE + 0x210, 0x18);
    qtest_writel(q, CAPTURE + 0x290, 0x80);
    pipeline_step(q, 999999);
    g_assert_cmphex(qtest_readl(q, WORK_BUFFER), ==, 0xa5a5a5a5);
    pipeline_step(q, 1);
    for (unsigned y = 0; y < 2; y++) {
        qtest_memread(q, WORK_BUFFER + 16 * y, row, sizeof(row));
        g_assert_cmpmem(row, 12, packed_white, sizeof(packed_white));
        g_assert_cmphex((uint32_t)ldl_le_p(row + 12), ==, 0xa5a5a5a5);
    }
    g_assert_cmpuint(property_at(q, WORK_PATH, "completed"), ==, 2);
    g_assert_cmpuint(property_at(q, WORK_PATH, "bytes"), ==, 48);
    qtest_writel(q, CAPTURE + 0x210, 0x18);
    qtest_writel(q, CAPTURE + 0x290, 0x80);
    pipeline_step(q, 2000000);
    g_assert_cmpuint(property_at(q, WORK_PATH, "completed"), ==, 2);
    qtest_quit(q);
    unlink(bundle);
    unlink(path);
}

static void test_capture_work_submitted_cancel(void)
{
    for (unsigned control = 0; control < 3; control++) {
        g_autofree char *bundle = NULL;
        QTestState *q = pipeline_start(&bundle, NULL);
        uint64_t published;

        work_configure(q, 2, true);
        main_input_rise(q);
        pipeline_step(q, 3);
        /* Submit at the minimum; cancellation may race a completed publication. */
        qtest_clock_step(q, 1000000);
        if (control == 2) {
            qtest_system_reset(q);
        } else if (control == 1) {
            qtest_writel(q, 0x20069514, 4);
            qtest_writel(q, CAPTURE + 0x104, 0x80);
        } else {
            qtest_writel(q, CAPTURE + 0xc, 0x200);
        }
        published = property_at(q, WORK_PATH, "completed");
        g_assert_cmpuint(published, <=, 1);
        g_assert_cmphex(qtest_readl(q, WORK_BUFFER + 0x1000), ==,
                       published ? 0 : 0xa5a5a5a5);
        g_assert_cmpuint(property_at(q, WORK_PATH, "cancelled"), ==,
                         1 - published);
        /* Reconfigure and deliver new input before waiting on cancelled work. */
        work_configure(q, 0, false);
        qtest_writel(q, CAPTURE + 0x210, 0x18);
        qtest_writel(q, CAPTURE + 0x290, 0x80);
        main_input_rise(q);
        pipeline_step(q, 10000);
        pipeline_step(q, 2000000);
        for (unsigned y = 0; y < 2; y++) {
            for (unsigned x = 0; x < 8; x++) {
                static const uint16_t gray[2][2] = {
                    { 0, 0x4040 }, { 0x8080, 0xffff },
                };

                g_assert_cmpuint(qtest_readw(q, WORK_BUFFER + 0x1000 +
                                            y * 16 + x * 2), ==,
                                 gray[y][x / 4]);
            }
        }
        g_assert_cmphex(qtest_readl(q, WORK_BUFFER), ==, 0xa5a5a5a5);
        g_assert_cmphex(qtest_readl(q, WORK_BUFFER + 0x1020), ==, 0xa5a5a5a5);
        g_assert_cmpuint(property_at(q, WORK_PATH, "completed"), ==,
                         published + 1);
        qtest_writel(q, CAPTURE + 0x210, 0x18);
        qtest_writel(q, CAPTURE + 0x290, 0x80);
        pipeline_step(q, 2000000);
        g_assert_cmpuint(property_at(q, WORK_PATH, "completed"), ==,
                         published + 1);
        qtest_quit(q);
        unlink(bundle);
    }
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
        pipeline_step(q, 2000000);
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
        pipeline_step(q, 2000000);
        bool main_invalid = i == 0 || i == ARRAY_SIZE(bad) - 1;

        g_assert_cmpuint(property_at(q, WORK_PATH, "rejected"), ==,
                         !main_invalid);
        g_assert_cmpuint(property_at(q, PIPELINE_PATH, "main-rejected"), ==,
                         main_invalid);
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
            pipeline_step(q, 2000000);
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
    /* A stopped unit1 cannot submit a MAIN acquisition. */
    qtest_writel(q, CAPTURE + 0xc, 1);
    work_input(q);
    qtest_writel(q, CAPTURE + 8, 1);
    work_rise(q);
    pipeline_step(q, 2000000);
    g_assert_cmpuint(property_at(q, WORK_PATH, "completed"), ==, 0);
    main_input_rise(q);
    pipeline_step(q, 10000);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "main-completed"), ==, 1);
    pipeline_step(q, 2000000);
    g_assert_cmpuint(property_at(q, WORK_PATH, "completed"), ==, 1);
    g_assert_cmphex(qtest_readl(q, WORK_BUFFER + 0x1000), ==, 0);
    g_assert_cmphex(qtest_readl(q, CAPTURE + 0x210) & 0x18, ==, 0x18);
    g_assert_cmphex(qtest_readl(q, CAPTURE + 0x290) & 0x80, ==, 0x80);
    /* A real prior completion owns the output until the matching guest ACK. */
    uint64_t blocked = property_at(q, WORK_PATH, "blocked");

    statistics_input(q, NULL);
    g_assert_cmpuint(property_at(q, WORK_PATH, "blocked"), ==, blocked + 1);
    g_assert_cmphex(qtest_readl(q, WORK_BUFFER), ==, 0xa5a5a5a5);
    work_rise(q);
    g_assert_cmpuint(property_at(q, WORK_PATH, "blocked"), ==, blocked + 2);
    work_ack(q);
    work_rise(q);
    pipeline_step(q, 2000000);
    g_assert_cmpuint(property_at(q, WORK_PATH, "completed"), ==, 2);
    g_assert_cmphex(qtest_readl(q, WORK_BUFFER), ==, 0);
    work_ack(q);
    /* Stop/start cannot replay the delivered generation into the next slot. */
    qtest_memset(q, WORK_BUFFER + 0x1000, 0xa5, 32);
    qtest_writel(q, CAPTURE + 0xc, 0x200);
    qtest_writel(q, CAPTURE + 8, 0x200);
    work_rise(q);
    pipeline_step(q, 2000000);
    g_assert_cmpuint(property_at(q, WORK_PATH, "completed"), ==, 2);
    g_assert_cmphex(qtest_readl(q, WORK_BUFFER + 0x1000), ==, 0xa5a5a5a5);
    work_input(q);
    work_rise(q);
    pipeline_step(q, 2000000);
    g_assert_cmpuint(property_at(q, WORK_PATH, "completed"), ==, 3);
    g_assert_cmphex(qtest_readl(q, WORK_BUFFER + 0x1000), ==, 0);
    qtest_quit(q);
    unlink(bundle);
}

static void test_capture_work_main_domain(void)
{
    static const char header[] = "P6\n8 8\n255\n";
    static const uint8_t rows[4][6] = {
        { 0x01, 0x21, 0x20, 0x03, 0x23, 0x20 },
        { 0x02, 0x32, 0x30, 0x02, 0x52, 0x50 },
        { 0x01, 0x41, 0x40, 0x03, 0x43, 0x40 },
        { 0x04, 0x54, 0x50, 0x04, 0x74, 0x70 },
    };
    g_autofree char *bundle = NULL;
    g_autofree char *scene = NULL;
    QTestState *q = pipeline_start(&bundle, NULL);
    uint8_t image[sizeof(header) - 1 + 8 * 8 * 3];
    uint8_t expected[96], actual[sizeof(expected)];
    uint32_t auxiliary = RAM + 0x400000;
    int fd = g_file_open_tmp("nikon-main-domain-XXXXXX", &scene, NULL);

    g_assert_cmpint(fd, >=, 0);
    close(fd);
    memcpy(image, header, sizeof(header) - 1);
    for (unsigned y = 0; y < 8; y++) {
        for (unsigned x = 0; x < 8; x++) {
            unsigned at = sizeof(header) - 1 + (y * 8 + x) * 3;

            image[at] = 16 + 16 * (x / 2);
            image[at + 1] = 32 + 16 * (y / 2);
            image[at + 2] = 48 + 16 * (x / 2 + y / 2);
        }
    }
    g_assert_true(g_file_set_contents(scene, (const char *)image,
                                      sizeof(image), NULL));
    work_configure(q, 2, false);
    qtest_qmp_assert_success(q,
        "{'execute':'qom-set','arguments':{'path':'/machine/cmos',"
        "'property':'image','value':%s}}", scene);
    qtest_writel(q, CAPTURE + 0x1d00, 8 << 16);
    qtest_writel(q, CAPTURE + 0x1d04, 8 << 16);
    qtest_writel(q, CAPTURE + 0x1e78, 8 << 16);
    qtest_writel(q, CAPTURE + 0x1e7c, 8 << 16);
    qtest_writel(q, 0x20069518, 4 << 16);
    qtest_writel(q, 0x2006951c, 4 << 16);
    qtest_writel(q, CAPTURE + 0x100, 0x85);
    pipeline_configure(q, auxiliary, 16640, 16640 * 34,
                       NIKON_READOUT_C170_FORMAT_PRIMARY);
    pipeline_request(q);
    pipeline_step(q, 10000);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "completed"), ==, 1);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "main-completed"), ==, 1);
    g_assert_cmphex(qtest_readl(q, auxiliary), ==, 0x20201010);
    /* One edge produces both views. MAIN consumers retain MAIN's independent
     * pixels after the later auxiliary snapshot replaces the legacy input. */
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "main-input-generation"), ==,
                     1);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "main-input-width"), ==, 8);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "main-input-height"), ==, 8);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "main-input-stride"), ==,
                     16);
    work_ack(q);
    work_rise(q);
    pipeline_step(q, 2000000);
    g_assert_cmpuint(property_at(q, WORK_PATH, "completed"), ==, 1);
    g_assert_cmpuint(property_at(q, WORK_PATH, "bytes"), ==, 24);
    memset(expected, 0xa5, sizeof(expected));
    for (unsigned y = 0; y < 4; y++) {
        memcpy(expected + 16 + y * 16, rows[y], sizeof(rows[y]));
    }
    qtest_memread(q, WORK_BUFFER + 0x1000 - 16, actual, sizeof(actual));
    g_assert_cmpmem(actual, sizeof(actual), expected, sizeof(expected));
    g_assert_cmphex(qtest_readl(q, CAPTURE + 0x210) & 0x18, ==, 0x18);
    g_assert_cmphex(qtest_readl(q, CAPTURE + 0x290), ==, 0x80);
    work_ack(q);
    /* The latest auxiliary generation is larger than MAIN's independent1.
     * A run/configuration floor must stay in the MAIN namespace.
     */
    qtest_writel(q, CAPTURE + 0xc, 0x200);
    qtest_writel(q, CAPTURE + 8, 0x200);
    work_rise(q);
    pipeline_step(q, 2000000);
    g_assert_cmpuint(property_at(q, WORK_PATH, "completed"), ==, 1);
    qtest_writel(q, 0x20069a3c, 2);
    qtest_writel(q, CAPTURE + 0x104, 0x80);
    work_rise(q);
    pipeline_step(q, 2000000);
    g_assert_cmpuint(property_at(q, WORK_PATH, "completed"), ==, 1);

    /* Auxiliary cancellation cannot revoke independently delivered MAIN. */
    qtest_writel(q, READOUT_PARAMETERS + 0x400, 1);
    qtest_writel(q, CAPTURE + 0xc, 2);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "main-input-generation"), ==,
                     1);
    main_input_rise(q);
    pipeline_step(q, 10000);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "main-input-generation"), ==,
                     2);
    /* MAIN stop clears the legacy reference before freeing MAIN-owned bytes. */
    qtest_writel(q, CAPTURE + 0xc, 1);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "main-input-generation"), ==,
                     0);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "main-input-width"), ==, 0);
    work_rise(q);
    pipeline_step(q, 2000000);
    g_assert_cmpuint(property_at(q, WORK_PATH, "completed"), ==, 1);
    qtest_system_reset(q);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "main-input-generation"), ==,
                     0);
    g_assert_cmpuint(property_at(q, PIPELINE_PATH, "main-input-height"), ==, 0);
    qtest_quit(q);
    unlink(scene);
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

static void paired_denoise(QTestState *q, uint32_t base, unsigned threshold,
                           unsigned low, unsigned axial, unsigned diagonal,
                           unsigned bias, unsigned retain, unsigned count)
{
    uint32_t words[] = {
        threshold, (low << 16) | (low + 2),
        (axial << 24) | (diagonal << 16) | (bias << 8) | bias,
        retain * 0x01010101U, 0, 0x03020001, 0,
    };

    for (unsigned i = 0; i < count; i++) {
        qtest_writel(q, base + 0x660 + 4 * i, words[i]);
    }
}

static void paired_detail(QTestState *q, uint32_t base, unsigned limit,
                          unsigned strength, unsigned lane, unsigned count)
{
    uint32_t words[] = {
        0x00060301, 0, 0x40000600 | (limit << 16) | strength,
        0x04000000 | lane * 0x010101U,
        0x00040010, 0, 0x001f0000, 0x1f100804,
    };

    for (unsigned i = 0; i < count; i++) {
        qtest_writel(q, base + 0x73c + 4 * i, words[i]);
    }
}

static void paired_adaptive(QTestState *q, uint32_t base, unsigned dose,
                            unsigned count)
{
    uint32_t words[] = {
        (50 << 16) | 30, (60 << 16) | 4095, (1024 << 16) | 4095,
        0x081f0c0e, 0x04000000 | (dose << 16),
        0x08080808, 0x00000808, 0x0af00020,
    };

    for (unsigned i = 0; i < count; i++) {
        qtest_writel(q, base + 0x75c + 4 * i, words[i]);
    }
}

static void paired_radial(QTestState *q, uint32_t base, unsigned cx,
                          unsigned cy,
                          int red, int blue, unsigned count)
{
    uint32_t words[] = {
        270, (cy << 16) | cx, 0x00030205,
        ((red & 0x3ffU) << 16) | (blue & 0x3ffU), 0x0520, 0x0101,
    };

    for (unsigned i = 0; i < count; i++) {
        qtest_writel(q, base + 0x724 + 4 * i, words[i]);
    }
}

static void paired_saturation(QTestState *q, uint32_t base, unsigned slot,
                              unsigned first, unsigned last, unsigned words)
{
    for (unsigned word = 0; word < words; word++) {
        unsigned i = 2 * word;
        unsigned low = ((32 - i) * first + i * last + 16) / 32;
        unsigned high = i == 32 ? 0 :
            ((31 - i) * first + (i + 1) * last + 16) / 32;

        qtest_writel(q, base + 0x1a08 + 68 * slot + 4 * word,
                     low | (high << 16));
    }
}

static void paired_colorfilter(QTestState *q, uint32_t base, unsigned strength,
                               unsigned count, bool zero_tail)
{
    uint32_t words[51] = { 0 };

    words[0x0c / 4] = 0x02020202;
    words[0x1c / 4] = 0x02020202;
    words[0x54 / 4] = 0x0bb80190;
    words[0x58 / 4] = 0x10808030;
    words[0x68 / 4] = 0x02020302;
    words[0x78 / 4] = 0x02020202;
    words[0xac / 4] = 0x00050009;
    words[0xb8 / 4] = (130 << 8) | strength;
    words[0xbc / 4] = (2 << 16) | 200;
    words[0xc4 / 4] = (16 << 8) | 4;
    if (zero_tail) {
        words[0xb8 / 4] = 0;
        words[0xbc / 4] = 2 << 16;
        words[0xc4 / 4] = (8 << 8) | 4;
    }
    for (unsigned i = 0; i < count; i++) {
        unsigned offset = i < 45 ? 0x900 + 4 * i : i == 45 ? 0x9d8 :
                          i == 46 ? 0x9b4 : 0x9b8 + 4 * (i - 47);

        qtest_writel(q, base + offset,
                     words[offset == 0x9d8 ? 50 : (offset - 0x900) / 4]);
    }
}

static void paired_post_config(QTestState *q, uint32_t base, unsigned count)
{
    const unsigned ranges[][2] = {
        { 0xb38, 0xb58 }, { 0xb94, 0xbbc },
        { 0xd20, 0xd9c }, { 0xdc0, 0xdec },
    };
    unsigned word = 0;

    for (unsigned group = 0; group < ARRAY_SIZE(ranges); group++) {
        for (unsigned offset = ranges[group][0]; offset <= ranges[group][1];
             offset += 4, word++) {
            uint32_t value = offset == 0xbac ? 256 :
                             offset == 0xdd4 ? 3 : offset == 0xdd8 ? 5 : 0;

            if (offset == 0xdc0 || offset == 0xdc4) {
                value = 10 << 8;
            } else if (offset == 0xdd0) {
                value = 0x080401;
            } else if (offset == 0xde4) {
                value = 0x8080;
            } else if (offset == 0xde8) {
                value = 0x010101;
            }

            if (word < count) {
                qtest_writel(q, base + offset, value);
            }
        }
    }
}

static int64_t post_round(int64_t value, int64_t divisor)
{
    lldiv_t q = lldiv(value, divisor);

    if (2 * llabs(q.rem) >= divisor) {
        q.quot += value < 0 ? -1 : 1;
    }
    return q.quot;
}

static void paired_post_oracle(uint16_t *image, unsigned width, unsigned height,
                               unsigned unsharp, unsigned axial,
                               unsigned diagonal, unsigned edge)
{
    static const int offsets[][2] = {
        { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 },
        { -1, -1 }, { 1, -1 }, { -1, 1 }, { 1, 1 },
    };
    g_autofree uint16_t *source = g_new(uint16_t, width * height);

    for (unsigned stage = 0; stage < 4; stage++) {
        memcpy(source, image, width * height * sizeof(*source));
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                unsigned at = y * width + x;
                int64_t n = 0, divisor;

                for (unsigned i = 0; i < 4; i++) {
                    const int *delta = offsets[i + (stage == 2 ? 4 : 0)];
                    int xx = CLAMP(x + delta[0], 0, (int)width - 1);
                    int yy = CLAMP(y + delta[1], 0, (int)height - 1);

                    n += source[yy * width + xx] - source[at];
                }
                if (!stage) {
                    n *= -(int64_t)unsharp;
                    divisor = 1024;
                } else if (stage < 3) {
                    n *= stage == 1 ? axial : diagonal;
                    divisor = 128;
                } else {
                    n *= MAX(320 - llabs(n), 0) * edge;
                    divisor = 40960;
                }
                image[at] = CLAMP(source[at] + post_round(n, divisor), 0, 4095);
            }
        }
    }
}

typedef struct PostPacketFixture {
    unsigned negative, positive, slope, retain, amount, modifier;
    unsigned mix, radius, diagonal;
} PostPacketFixture;

static void post_packet_oracle(uint16_t *image, unsigned width, unsigned height,
                                const PostPacketFixture *p)
{
    static const int delta[8][2] = {
        { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 },
        { -1, -1 }, { 1, -1 }, { -1, 1 }, { 1, 1 },
    };
    g_autofree uint16_t *original = g_new(uint16_t, width * height);

    memcpy(original, image, width * height * sizeof(*image));

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            unsigned at = y * width + x;
            int center = original[at];
            int brightness = post_round(p->slope * center, 16);
            int negative = 16 * p->negative + brightness;
            int positive = 16 * p->positive + brightness;
            int64_t numerator = 0;
            uint64_t denominator = UINT64_C(64) * 31 * 256 * 512 *
                                   (128 + p->mix);

            for (unsigned i = 0; i < 8; i++) {
                int xx = CLAMP(x + delta[i][0] * (int)p->radius,
                               0, (int)width - 1);
                int yy = CLAMP(y + delta[i][1] * (int)p->radius,
                               0, (int)height - 1);
                int difference = CLAMP((int)original[yy * width + xx] - center,
                                       -negative, positive);

                numerator += (i < 4 ? 16 - p->diagonal : p->diagonal) *
                             (int64_t)difference;
            }
            numerator *= (31 - p->retain) * (int64_t)p->amount *
                         p->modifier * p->mix;
            image[at] = CLAMP(center + post_round(numerator, denominator),
                              0, 4095);
        }
    }
}

static void paired_configure(QTestState *q, unsigned table_mode, uint32_t base,
                             unsigned denoise_words, unsigned detail_words,
                             unsigned adaptive_words, unsigned radial_words,
                             unsigned colorfilter_words, unsigned post_words,
                             unsigned saturation_words)
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
        /* These independent MAIN/WORK fixtures select the optional-grid-off
         * profile. A connected unit8 producer must supply an enabled grid.
         */
        { 0x124, 1 }, { 0x12c, 0 }, { 0x204, (36 << 16) | 64 },
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
        if (table_mode == 3 && offset >= 0x1900 && offset <= 0x197c) {
            continue;
        }
        if (offset < 0x1a08 || offset > 0x1a8c) {
            qtest_writel(q, base + offset,
                         offset >= 0x1900 && offset <= 0x197c ? 0x01000100 : 0);
        }
    }
    paired_post_config(q, base, post_words);
    paired_saturation(q, base, 0, 256, 256, saturation_words);
    paired_matrix(q, base, 0xb00, a, 13, false);
    paired_matrix(q, base, 0xb64, b, 12, false);
    paired_matrix(q, base, 0xb94, c, 13, true);
    /* Explicit finite neutral parameters; no omitted/zero NR bypass. */
    paired_denoise(q, base, 7, 1, 3, 1, 0, 31, denoise_words);
    paired_detail(q, base, 4, 0, 8, detail_words);
    paired_adaptive(q, base, 0, adaptive_words);
    paired_radial(q, base, 32, 18, 0, 0, radial_words);
    paired_colorfilter(q, base, 0, colorfilter_words, true);
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
        g_assert_cmphex(base, ==, PAIRED_GRID);
        /* A confidence map is input, never an output of paired processing. */
    }
    qtest_memread(q, base + phase * (base == PAIRED_GRID ? 0x1000 : 0x2000)
                     - 16, actual, sizeof(actual));
    g_assert_cmpmem(actual, sizeof(actual), expected, sizeof(expected));
}

static QTestState *paired_start(char **bundle, char **scene,
                                unsigned table_mode, bool arm)
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
    paired_configure(q, table_mode, PAIRED, 7, 8, 8, 6, 51, 64, 17);
    if (arm) {
        qtest_writel(q, PAIRED + 0x10, 1);
        qtest_writel(q, PAIRED + 8, 1);
    }
    return q;
}

static void paired_untouched(QTestState *q);
static void paired_main_at(QTestState *q, uint32_t address, uint8_t luma,
                           uint8_t u, uint8_t v);

static void paired_raw_input(QTestState *q)
{
    work_input(q);
    work_rise(q);
    /* Actual RAW publication starts paired's own independent 1 ms deadline. */
    pipeline_step(q, 1000000);
}

/* Complete source store order, including interleaved channels and suffixes. */
static unsigned paired_online_offsets(unsigned group, unsigned offsets[129])
{
    static const unsigned short_groups[][12] = {
        { 0xdf4, 0xdf8, 0xdfc, 0xe00, 0xe0c, 0xe04, 0xe08, 0xe10 },
        { 0x9d0, 0x9d4, 0x9cc },
        { 0x724, 0x728, 0x72c, 0x730, 0x734, 0x738 },
    };
    static const unsigned counts[] = { 8, 3, 6 };
    static const unsigned ranges[][2] = {
        { 0x660, 0x678 }, { 0, 0 }, { 0x708, 0x720 },
        { 0x73c, 0x758 }, { 0x75c, 0x778 }, { 0, 0 },
        { 0xb00, 0xb10 }, { 0xb64, 0xb74 }, { 0, 0 },
        { 0xd20, 0xd9c }, { 0, 0 }, { 0xb58, 0xb58 },
        { 0x2890, 0x2a90 }, { 0x2a94, 0x2c94 },
    };
    unsigned count = 0;

    if (group < 3) {
        count = counts[group];
        memcpy(offsets, short_groups[group], count * sizeof(*offsets));
    } else if (group == 4) {
        offsets[count++] = 0x594;
        for (unsigned channel = 0; channel < 3; channel++) {
            for (unsigned field = 0; field < 6; field++) {
                offsets[count++] = 0x598 + 4 * channel + 12 * field;
            }
        }
        offsets[count++] = 0x608;
    } else if (group == 8) {
        for (unsigned offset = 0x900; offset <= 0x9b0; offset += 4) {
            offsets[count++] = offset;
        }
        offsets[count++] = 0x9d8;
        offsets[count++] = 0x9b4;
        for (unsigned offset = 0x9b8; offset <= 0x9c4; offset += 4) {
            offsets[count++] = offset;
        }
    } else if (group == 11) {
        for (unsigned offset = 0xb94; offset <= 0xba4; offset += 4) {
            offsets[count++] = offset;
        }
        offsets[count++] = 0xbac;
        for (unsigned offset = 0xbb0; offset <= 0xbbc; offset += 4) {
            offsets[count++] = offset;
        }
        offsets[count++] = 0xba8;
    } else if (group == 13) {
        static const unsigned edge[] = {
            0xdc0, 0xdc4, 0xdc8, 0xdcc, 0xdd0, 0xddc, 0xde0, 0xde8,
            0xdec, 0xdd4, 0xdd8, 0xde4,
        };

        count = ARRAY_SIZE(edge);
        memcpy(offsets, edge, sizeof(edge));
    } else {
        g_assert_cmpuint(group, <, 17);
        for (unsigned offset = ranges[group - 3][0];
             offset <= ranges[group - 3][1]; offset += 4) {
            offsets[count++] = offset;
        }
    }
    return count;
}

static void paired_online_republish(QTestState *q, unsigned offset, uint32_t value)
{
    unsigned offsets[129];
    uint32_t words[129];

    for (unsigned group = 0; group < 17; group++) {
        unsigned count = paired_online_offsets(group, offsets);
        bool found = false;

        for (unsigned i = 0; i < count; i++) {
            words[i] = qtest_readl(q, PAIRED + offsets[i]);
            if (offsets[i] == offset) {
                words[i] = value;
                found = true;
            }
        }
        if (found) {
            for (unsigned i = 0; i < count; i++) {
                qtest_writel(q, PAIRED + offsets[i], words[i]);
            }
            return;
        }
    }
    g_assert_not_reached();
}

static QTestState *paired_online_start(char **bundle, char **scene);

static void paired_cfa_input(QTestState *q)
{
    qtest_writel(q, CAPTURE + 0x1d00, 72 << 16);
    qtest_writel(q, CAPTURE + 0x1e78, 72 << 16);
    qtest_writel(q, CAPTURE + 0x100, 0x85);
    paired_raw_input(q);
    pipeline_step(q, 1000000);
}

static void paired_cfa_reduction(unsigned rows)
{
    static const char header[] = "P6\n64 72\n255\n";
    g_autofree char *bundle = NULL;
    g_autofree char *scene = NULL;
    QTestState *q = paired_start(&bundle, &scene, true, false);
    uint8_t ppm[sizeof(header) - 1 + 64 * 72 * 3], row[96];

    memcpy(ppm, header, sizeof(header) - 1);
    for (unsigned y = 0; y < 72; y++) {
        for (unsigned x = 0; x < 64; x++) {
            unsigned at = sizeof(header) - 1 + 3 * (y * 64 + x);

            ppm[at] = 8 + x;
            ppm[at + 1] = 32 + y;
            ppm[at + 2] = 64 + x + y;
        }
    }
    g_assert_true(g_file_set_contents(scene, (const char *)ppm,
                                      sizeof(ppm), NULL));
    qtest_qmp_assert_success(q,
        "{'execute':'qom-set','arguments':{'path':'/machine/cmos',"
        "'property':'image','value':%s}}", scene);
    qtest_writel(q, 0x2006951c, rows << 16);
    paired_cfa_input(q);
    g_assert_cmpuint(property_at(q, WORK_PATH, "completed"), ==, rows == 36);
    g_assert_cmpuint(property_at(q, WORK_PATH, "rejected"), ==, rows != 36);
    if (rows != 36) {
        g_assert_cmphex(qtest_readl(q, WORK_BUFFER + 0x1000), ==, 0xa5a5a5a5);
        goto out;
    }
    for (unsigned y = 0; y < 36; y++) {
        unsigned sy = 4 * (y / 2) + y % 2;

        qtest_memread(q, WORK_BUFFER + 0x1000 + 96 * y, row, sizeof(row));
        for (unsigned x = 0; x < 64; x++) {
            unsigned phase = 2 * (y % 2) + x % 2;
            unsigned sample = phase == 0 ? 8 + x :
                              phase == 3 ? 64 + x + sy : 32 + sy;
            unsigned bit = x * 12;
            unsigned packed = lduw_le_p(row + bit / 8);

            g_assert_cmpuint((packed >> (bit % 8)) & 4095, ==,
                             (sample * 257) >> 4);
        }
    }
    g_assert_cmphex(qtest_readl(q, WORK_BUFFER + 0x1000 - 4), ==, 0xa5a5a5a5);
    g_assert_cmphex(qtest_readl(q, WORK_BUFFER + 0x1000 + 96 * 36), ==,
                    0xa5a5a5a5);
out:
    paired_untouched(q);
    qtest_quit(q);
    unlink(scene);
    unlink(bundle);
}

static void test_paired_cfa_reduction(void)
{
    paired_cfa_reduction(36);
    paired_cfa_reduction(35);
}

static void test_paired_cfa_color(void)
{
    static const uint8_t colors[][3] = {
        { 128, 128, 128 }, { 192, 64, 32 }, { 32, 64, 192 },
    };
    static const uint8_t expected[][3] = {
        { 129, 128, 128 }, { 99, 90, 195 }, { 69, 198, 101 },
    };
    static const int identity[] = { 1024, 0, 0, 0, 1024, 0, 0, 0, 1024 };
    static const int yuv[] = { 153, 300, 59, -86, -170, 256, 256, -214, -42 };

    for (unsigned color = 0; color < ARRAY_SIZE(colors); color++) {
        g_autofree char *bundle = NULL;
        g_autofree char *scene = NULL;
        QTestState *q = paired_start(&bundle, &scene, true, false);
        uint8_t ppm[] = "P6\n1 1\n255\n\0\0\0";

        memcpy(ppm + sizeof(ppm) - 4, colors[color], 3);
        g_assert_true(g_file_set_contents(scene, (const char *)ppm,
                                          sizeof(ppm) - 1, NULL));
        qtest_qmp_assert_success(q,
            "{'execute':'qom-set','arguments':{'path':'/machine/cmos',"
            "'property':'image','value':%s}}", scene);
        paired_matrix(q, PAIRED, 0xb00, identity, 13, false);
        paired_matrix(q, PAIRED, 0xb64, yuv, 12, false);
        /* Odd MAIN crop exercises the encoded RAW origin without rephasing it. */
        qtest_writel(q, PAIRED + 0x400, (3 << 16) | 28);
        qtest_writel(q, PAIRED + 0x40c, (56 << 16) | 3);
        qtest_writel(q, PAIRED + 0x10, 1);
        qtest_writel(q, PAIRED + 8, 1);
        paired_cfa_input(q);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 1);
        paired_main_at(q, PAIRED_MAIN, expected[color][0],
                        expected[color][1], expected[color][2]);
        g_assert_cmphex(qtest_readl(q, PAIRED_MAIN - 4), ==, 0xa5a5a5a5);
        g_assert_cmphex(qtest_readl(q, PAIRED_MAIN + 56), ==, 0xa5a5a5a5);
        g_assert_cmphex(qtest_readl(q, PAIRED + 0x24), ==, 0x1002);
        qtest_quit(q);
        unlink(scene);
        unlink(bundle);
    }
}

static unsigned transfer_knot(unsigned index, unsigned scale)
{
    return scale * (index + index * index / 256);
}

static void transfer_config(QTestState *q, unsigned scale, unsigned words,
                            unsigned invalid)
{
    for (unsigned i = 0; i < words; i++) {
        unsigned lo = transfer_knot(2 * i, scale);
        unsigned hi = transfer_knot(2 * i + 1, scale);

        if (i == 64 && invalid == 1) {
            hi = lo; /* Ambiguous code. */
        } else if (i == 64 && invalid == 2) {
            hi = lo - 1;
        } else if (i == 64 && invalid == 3) {
            hi = 4096;
        }
        qtest_writel(q, 0x20069600 + 4 * i, lo | (hi << 16));
    }
    qtest_writel(q, 0x20069514, 6);
    qtest_writel(q, CAPTURE + 0x104, 0x85);
}

static void test_paired_transfer(void)
{
    static const uint8_t colors[][3] = {
        { 128, 128, 128 }, { 192, 64, 32 }, { 32, 64, 192 }, { 64, 128, 32 },
    };
    static const uint8_t expected[][3] = {
        { 129, 128, 128 }, { 99, 90, 195 }, { 69, 198, 101 }, { 129, 128, 128 },
    };
    static const int identity[] = { 1024, 0, 0, 0, 1024, 0, 0, 0, 1024 };
    static const int yuv[] = { 153, 300, 59, -86, -170, 256, 256, -214, -42 };

    for (unsigned color = 0; color < ARRAY_SIZE(colors); color++) {
        for (unsigned scale = 1; scale <= 4; scale += 3) {
            g_autofree char *bundle = NULL;
            g_autofree char *scene = NULL;
            QTestState *q = paired_start(&bundle, &scene, true, false);
            uint8_t ppm[] = "P6\n1 1\n255\n\0\0\0";
            uint8_t before[96 * 36], after[sizeof(before)];

            memcpy(ppm + sizeof(ppm) - 4, colors[color], 3);
            g_assert_true(g_file_set_contents(scene, (const char *)ppm,
                                              sizeof(ppm) - 1, NULL));
            qtest_qmp_assert_success(q,
                "{'execute':'qom-set','arguments':{'path':'/machine/cmos',"
                "'property':'image','value':%s}}", scene);
            transfer_config(q, scale, 128, 0);
            paired_matrix(q, PAIRED, 0xb00, identity, 13, false);
            paired_matrix(q, PAIRED, 0xb64, yuv, 12, false);
            if (color == 3) {
                /* Fixed sensor attenuation, followed by ordinary live WB. */
                qtest_writel(q, PAIRED + 0x9d0, (512 << 16) | 256);
                qtest_writel(q, PAIRED + 0x9d4, 1024);
                qtest_writel(q, PAIRED + 0x9cc, 0x01010101);
            }
            paired_cfa_input(q);
            g_assert_cmpuint(property_at(q, WORK_PATH, "completed"), ==, 1);
            qtest_memread(q, WORK_BUFFER + 0x1000, before, sizeof(before));
            for (unsigned y = 0; y < 36; y++) {
                for (unsigned x = 0; x < 64; x++) {
                    unsigned lane = 2 * (y & 1) + (x & 1);
                    unsigned channel = lane == 0 ? 0 : lane == 3 ? 2 : 1;
                    unsigned bit = x * 12;
                    unsigned code = lduw_le_p(before + y * 96 + bit / 8);

                    g_assert_cmpuint((code >> (bit & 7)) & 4095, ==,
                        transfer_knot(colors[color][channel], scale));
                }
            }
            /* This uncommitted live write must not replace frame metadata. */
            qtest_writel(q, 0x20069600, UINT32_MAX);
            qtest_writel(q, PAIRED + 0x10, 1);
            qtest_writel(q, PAIRED + 8, 1);
            pipeline_step(q, 2000000);
            g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 1);
            paired_main_at(q, PAIRED_MAIN, expected[color][0],
                            expected[color][1], expected[color][2]);
            g_assert_cmpuint(qtest_readw(q, PAIRED_WORK), ==,
                             color ? 1414 : 2056);
            g_assert_cmpuint(qtest_readw(q, PAIRED_WORK + 80 * 18), ==, 0);
            qtest_memread(q, WORK_BUFFER + 0x1000, after, sizeof(after));
            g_assert_cmpmem(before, sizeof(before), after, sizeof(after));
            g_assert_cmphex(qtest_readl(q, PAIRED + 0x24), ==, 0x1002);
            qtest_quit(q);
            unlink(scene);
            unlink(bundle);
        }
    }
}

static void test_paired_transfer_boundaries(void)
{
    for (unsigned variant = 0; variant < 7; variant++) {
        g_autofree char *bundle = NULL;
        g_autofree char *scene = NULL;
        QTestState *q = paired_start(&bundle, &scene, true, false);

        transfer_config(q, 1, variant == 3 ? 127 : 128,
                         variant < 3 ? variant + 1 : 0);
        if (variant == 6) {
            work_input(q);
            work_rise(q);
            qtest_writel(q, CAPTURE + 0xc, 0x200);
            pipeline_step(q, 2000000);
            g_assert_cmpuint(property_at(q, WORK_PATH, "completed"), ==, 0);
            g_assert_cmpuint(property_at(q, WORK_PATH, "cancelled"), ==, 1);
        } else if (variant >= 4) {
            paired_cfa_input(q);
            g_assert_cmpuint(property_at(q, WORK_PATH, "completed"), ==, 1);
            if (variant == 4) {
                /* Default P5 gray128 encodes192;193 is not any LUT knot. */
                qtest_writeb(q, WORK_BUFFER + 0x1000, 193);
            } else if (variant == 5) {
                qtest_system_reset(q);
            }
            if (variant != 5) {
                qtest_writel(q, PAIRED + 0x10, 1);
                qtest_writel(q, PAIRED + 8, 1);
            }
            pipeline_step(q, 2000000);
            g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 0);
            if (variant == 4) {
                g_assert_cmpuint(property_at(q, PAIRED_PATH, "rejected"), >, 0);
            }
        } else {
            paired_cfa_input(q);
            g_assert_cmpuint(property_at(q, WORK_PATH, "completed"), ==, 0);
        }
        paired_untouched(q);
        qtest_quit(q);
        unlink(scene);
        unlink(bundle);
    }
}

static void paired_outputs_untouched(QTestState *q)
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
}

static void paired_untouched(QTestState *q)
{
    paired_outputs_untouched(q);
    g_assert_cmphex(qtest_readl(q, PAIRED + 0x24), ==, 0);
    g_assert_false(qtest_get_irq(q, 99 - 32));
}

static void test_paired_processing(void)
{
    g_autofree char *bundle = NULL;
    g_autofree char *scene = NULL;
    QTestState *q = paired_start(&bundle, &scene, true, true);

    work_rise(q);
    pipeline_step(q, 2000000);
    g_assert_cmphex(qtest_readl(q, PAIRED_MAIN), ==, 0xa5a5a5a5);
    g_assert_cmphex(qtest_readl(q, PAIRED + 0x24), ==, 0);
    g_assert_false(qtest_get_irq(q, 99 - 32));
    for (unsigned phase = 0; phase < 4; phase++) {
        unsigned slot = (phase + 1) % 4;

        work_input(q);
        work_rise(q);
        pipeline_step(q, 1000000);
        g_assert_cmphex(qtest_readl(q, WORK_BUFFER + slot * 0x1000), ==,
                        0x08808808);
        pipeline_step(q, 1000000);
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
        pipeline_step(q, 2000000);
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
        0x10020000,           /* The final enabled MAIN destination is ROM. */
        PAIRED_WORK + 0x2000, /* MAIN cannot overwrite its phase's WORK. */
        0xfffffff0,           /* The final MAIN Y/UV extent cannot wrap PA32. */
    };

    for (unsigned i = 0; i < ARRAY_SIZE(targets); i++) {
        g_autofree char *bundle = NULL;
        g_autofree char *scene = NULL;
        QTestState *q = paired_start(&bundle, &scene, true, false);

        qtest_writel(q, PAIRED + 0x494, targets[i]);
        qtest_writel(q, PAIRED + 0x4a4, targets[i] + 64 * 28);
        qtest_writel(q, PAIRED + 0x10, 1);
        qtest_writel(q, PAIRED + 8, 1);
        paired_raw_input(q);
        pipeline_step(q, 1000000);
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
    static const struct { unsigned offset; uint32_t value; } strict[] = {
        { 0x700, 0x100 }, { 0x704, 1 }, { 0xb48, 0x00010001 },
        { 0x9c8, 1 }, { 0x3c4, 0x8000 },
    };

    for (unsigned operation = 0; operation < 3 + ARRAY_SIZE(strict); operation++) {
        g_autofree char *bundle = NULL;
        g_autofree char *scene = NULL;
        QTestState *q = paired_start(&bundle, &scene, true, true);

        work_input(q);
        work_rise(q);
        pipeline_step(q, 1000000);
        g_assert_cmpuint(property_at(q, WORK_PATH, "completed"), ==, 1);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 0);
        if (operation == 0) {
            qtest_system_reset(q);
        } else if (operation == 1) {
            qtest_writel(q, PAIRED + 0x98, 0x401);
        } else if (operation == 2) {
            /* Equal retained configuration and table writes do not cancel. */
            qtest_writel(q, PAIRED + 0x98, 0x400);
            qtest_writel(q, PAIRED + 0xb00, 1024);
            qtest_writel(q, PAIRED + 0x1400, 0);
        } else {
            /* Source-confirmed numeric uploads do not relax modes, topology,
             * the adjacent opaque 9c8 word, or unknown 3c4 control bits.
             */
            qtest_writel(q, PAIRED + strict[operation - 3].offset,
                         strict[operation - 3].value);
        }
        pipeline_step(q, 2000000);
        if (operation != 2) {
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
        paired_raw_input(q);
        pipeline_step(q, 1000000);
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

    paired_raw_input(q);
    pipeline_step(q, 1000000);
    g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 1);
    qtest_writel(q, PAIRED + 0x24, UINT32_MAX);
    work_ack(q);
    work_input(q);
    work_rise(q);
    pipeline_step(q, 1000000);
    qtest_writel(q, PAIRED + 0x98, 0x401);
    pipeline_step(q, 2000000);
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

    paired_raw_input(q);
    pipeline_step(q, 1000000);
    g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 1);
    work_ack(q); /* Leave the paired result pending while RAW continues. */
    paired_raw_input(q);
    pipeline_step(q, 1000000);
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
    paired_raw_input(q);
    pipeline_step(q, 1000000);
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
    static const int c[9] = { 1024, 0, 0, 0, 0, -1024, 0, 1024, 0 };
    g_autofree char *bundle = NULL;
    g_autofree char *scene = NULL;
    QTestState *q = paired_start(&bundle, &scene, true, false);
    uint8_t expected[0xce0], actual[sizeof(expected)];

    paired_matrix(q, PAIRED, 0xb94, c, 13, true);
    qtest_writel(q, PAIRED + 0xbac, 0x00800080);
    qtest_writel(q, PAIRED + 0x10, 1);
    qtest_writel(q, PAIRED + 8, 1);
    paired_raw_input(q);
    pipeline_step(q, 1000000);
    /* B gives Y2056/U257/V-514. C rotates chroma to514/257. The half
     * blend gives U386/V-129 (signed ties away), before adding2048.
     */
    memset(expected, 0xa5, sizeof(expected));
    for (unsigned y = 0; y < 28; y++) {
        memset(expected + 16 + 64 * y, 129, 56);
    }
    for (unsigned y = 0; y < 14; y++) {
        for (unsigned x = 0; x < 56; x += 2) {
            expected[16 + 64 * (28 + y) + x] = 152;
            expected[16 + 64 * (28 + y) + x + 1] = 120;
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
        { 0x12c, 2 },         /* Unknown optional-grid control rejects. */
        { 0x218, 0 },         /* The primary front/back ring must agree. */
        { 0x228, PAIRED_MAIN }, /* Partial or unrelated second rings reject. */
        { 0xfc, 1 },          /* Secondary selection needs every mirror. */
        { 0x450, PAIRED_WORK + 4 }, /* Enabled WORK mirrors must agree. */
        { 0x4a8, PAIRED_MAIN }, /* Main Y alone is not a complete Y/UV group. */
        { 0x4b8, PAIRED_MAIN + 64 * 28 }, /* UV alone is also incomplete. */
    };

    for (unsigned i = 0; i < ARRAY_SIZE(invalid); i++) {
        g_autofree char *bundle = NULL;
        g_autofree char *scene = NULL;
        QTestState *q = paired_start(&bundle, &scene, true, false);

        if (invalid[i].offset == 0x228 ||
            invalid[i].offset == 0x4a8 || invalid[i].offset == 0x4b8 ||
            (invalid[i].offset == 0xfc && invalid[i].value == 1)) {
            paired_secondary(q, 0);
            if (invalid[i].offset == 0x4a8 || invalid[i].offset == 0x4b8 ||
                invalid[i].offset == 0xfc) {
                for (unsigned p = 0; p < 8; p++) {
                    qtest_writel(q, PAIRED + 0x4a8 + 4 * p, 0);
                }
            }
            qtest_writel(q, PAIRED + 0xfc, 1);
        }
        qtest_writel(q, PAIRED + invalid[i].offset, invalid[i].value);
        if (qtest_readl(q, PAIRED + 0xfc) == 1) {
            for (unsigned phase = 0; phase < 4; phase++) {
                qtest_writel(q, PAIRED + 0x228 + 4 * phase,
                             qtest_readl(q, PAIRED + 0x228 + 4 * phase));
                qtest_writel(q, PAIRED + 0x320 + 4 * phase,
                             qtest_readl(q, PAIRED + 0x320 + 4 * phase));
            }
        }
        qtest_writel(q, PAIRED + 0x10, 1);
        qtest_writel(q, PAIRED + 8, 1);
        paired_raw_input(q);
        pipeline_step(q, 1000000);
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

    for (unsigned mode = 0; mode < 7; mode++) {
        bool invalidate = mode == 1;
        g_autofree char *bundle = NULL;
        g_autofree char *scene = NULL;
        QTestState *q = paired_start(&bundle, &scene, true, true);
        uint8_t expected[0xce0], actual[sizeof(expected)];

        if (mode >= 4) {
            static const uint32_t affine[] = {
                0x02000200, 0x02000200, 0, 0, 0, 0, 32767,
            };

            qtest_writel(q, PAIRED + 0x700, 0x101);
            for (unsigned i = 0; i < ARRAY_SIZE(affine); i++) {
                qtest_writel(q, PAIRED + 0x708 + 4 * i, affine[i]);
            }
        }

        /* Two successful publications establish both aliased history units. */
        for (unsigned phase = 0; phase < 2; phase++) {
            paired_raw_input(q);
            pipeline_step(q, 1000000);
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
            static const unsigned offsets[] = {
                0xdf4, 0xdf8, 0xdfc, 0xe00, 0xe0c, 0xe04, 0xe08, 0xe10,
            };

            for (unsigned i = 0; i < ARRAY_SIZE(offsets); i++) {
                uint32_t value = offsets[i] == 0xdf8 ? 0xf8040000 :
                                 qtest_readl(q, PAIRED + offsets[i]);

                qtest_writel(q, PAIRED + offsets[i], value);
            }
        } else if (mode == 4) {
            paired_online_republish(q, 0x70c, 0x02000201);
        } else if (mode >= 5) {
            /* Both normal mode transitions retain pre-color history. */
            qtest_writel(q, PAIRED + 0x700, 0x100);
            qtest_writel(q, PAIRED + 0x704, mode == 6);
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
        paired_raw_input(q);
        pipeline_step(q, 1000000);
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
        paired_pixels(q, PAIRED_GRID, 2);
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

        qtest_writel(q, PAIRED + 0x9d0, (384 << 16) | 128);
        qtest_writel(q, PAIRED + 0x9d4, 64);
        qtest_writel(q, PAIRED + 0x9cc, cases[mode].control);
        qtest_writel(q, PAIRED + 0x10, 1);
        qtest_writel(q, PAIRED + 8, 1);
        paired_raw_input(q);
        pipeline_step(q, 1000000);
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
    static const uint32_t controls[][2] = {
        { 0x101, 0 }, { 0x100, 0 }, { 0x100, 1 },
    };

    for (unsigned mode = 0; mode < ARRAY_SIZE(controls); mode++) {
        g_autofree char *bundle = NULL;
        g_autofree char *scene = NULL;
        QTestState *q = paired_start(&bundle, &scene, true, false);

        qtest_writel(q, PAIRED + 0x700, controls[mode][0]);
        qtest_writel(q, PAIRED + 0x704, controls[mode][1]);
        qtest_writel(q, PAIRED + 0x708, (768 << 16) | 512);
        qtest_writel(q, PAIRED + 0x70c, (512 << 16) | 256);
        qtest_writel(q, PAIRED + 0x710, (0x1ff0 << 16) | 16);
        qtest_writel(q, PAIRED + 0x714, (0x1ff0 << 16) | 16);
        qtest_writel(q, PAIRED + 0x718, (16 << 16) | 32);
        qtest_writel(q, PAIRED + 0x71c, (64 << 16) | 128);
        qtest_writel(q, PAIRED + 0x720, 4095);
        qtest_writel(q, PAIRED + 0x10, 1);
        qtest_writel(q, PAIRED + 8, 1);
        paired_raw_input(q);
        pipeline_step(q, 1000000);
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
}

static void test_paired_quad_boundaries(void)
{
    static const struct { unsigned offset; uint32_t value; } invalid[] = {
        { 0x700, 1 }, { 0x700, 0x102 }, { 0x704, 1 }, { 0x704, 0x100 },
        { 0x708, 0x40000200 }, { 0x70c, 0x02004000 },
        { 0x710, 0x20000000 }, { 0x714, 0x2000 },
        { 0x720, 0x8000 }, { 0x720, 0x10000 },
    };

    for (unsigned i = 0; i < ARRAY_SIZE(invalid) + 3; i++) {
        g_autofree char *bundle = NULL;
        g_autofree char *scene = NULL;
        QTestState *q = paired_start(&bundle, &scene, true, false);

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
        pipeline_step(q, 1000000);
        if (i == ARRAY_SIZE(invalid)) {
            qtest_system_reset(q);
        } else if (i > ARRAY_SIZE(invalid)) {
            paired_online_republish(q, 0x70c,
                         i == ARRAY_SIZE(invalid) + 1 ?
                         0x02000201 : 0x02000200);
        }
        pipeline_step(q, 2000000);
        if (i > ARRAY_SIZE(invalid)) {
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
                             i > ARRAY_SIZE(invalid) ? 0 : 1);
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
        paired_raw_input(q);
        pipeline_step(q, 1000000);
        g_assert_cmphex(qtest_readl(q, WORK_BUFFER + 0x1000), ==, 0x08808808);
        paired_main_oracle(q, cases[mode].y, cases[mode].u, cases[mode].v);
        /* Curves affect MAIN color only; WORK history and grid input persist. */
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
        paired_raw_input(q);
        pipeline_step(q, 1000000);
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
        pipeline_step(q, 1000000);
        g_assert_cmpuint(property_at(q, WORK_PATH, "completed"), ==, 1);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 0);
        if (table) {
            /* First publication changes coverage despite equal zero values. */
            qtest_writel(q, PAIRED + 0x1400, 0);
        } else {
            /* A published zero curve differs from an absent curve. */
            paired_curve(q, 0, 0, 129, false);
        }
        pipeline_step(q, 2000000);
        if (!table) {
            g_assert_cmpuint(property_at(q, PAIRED_PATH, "cancelled"), ==, 0);
            g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 1);
            paired_main_oracle(q, 129, 144, 96);
            paired_pixels(q, PAIRED_WORK, 0);
            paired_pixels(q, PAIRED_GRID, 0);
            qtest_writel(q, PAIRED + 0x24, UINT32_MAX);
            work_ack(q);
            paired_raw_input(q);
            pipeline_step(q, 1000000);
            g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 2);
            paired_main_at(q, PAIRED_MAIN + 0x2000, 0, 128, 128);
            paired_pixels(q, PAIRED_WORK, 1);
            paired_pixels(q, PAIRED_GRID, 1);
            qtest_quit(q);
            unlink(scene);
            unlink(bundle);
            continue;
        }
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "cancelled"), ==, 1);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 0);
        paired_untouched(q);
        if (table) {
            /* A partial table must not resume the admitted RAW. */
            qtest_writel(q, PAIRED + 0x10, 1);
            pipeline_step(q, 2000000);
            g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 0);
            paired_untouched(q);
            for (unsigned offset = 0x1404; offset < 0x1440; offset += 4) {
                qtest_writel(q, PAIRED + offset, 0);
            }
        }
        /* The cancelled job never consumed its actual RAW generation. */
        qtest_writel(q, PAIRED + 0x10, 1);
        pipeline_step(q, 2000000);
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
    qtest_writel(q, PAIRED + 0xe04, 0);
    qtest_writel(q, PAIRED + 0xe08, 0);
    qtest_writel(q, PAIRED + 0xe10, 0x03fc03fc);
}

static void paired_noise_oracle(QTestState *q, unsigned phase, bool outside,
                                 uint32_t feedback)
{
    static const uint8_t first_rows[][2][4] = {
        { { 143, 128, 115, 114 }, { 120, 116, 113, 113 } },
        { { 129, 137, 140, 126 }, { 125, 135, 135, 140 } },
    };
    uint8_t pixels[64 * 42];
    uint16_t expected[56 * 28];
    uint32_t state = 1;
    for (unsigned y = 0; y < 28; y += 2) {
        for (unsigned x = 0; x < 56; x += 2) {
            for (unsigned dy = 0; dy < 2; dy++) {
                for (unsigned dx = 0; dx < 2; dx++) {
                    unsigned at = (y + dy) * 56 + x + dx;
                    bool outgoing = state & 1;
                    int noise;

                    state /= 2;
                    if (outgoing) {
                        state ^= feedback;
                    }
                    noise = CLAMP((int)(state >> 21) - 1024, -1020, 1020);

                    expected[at] = CLAMP(2056 + post_round(noise * 255, 1024),
                                         0, 4095);
                }
            }
        }
    }
    for (unsigned y = 0; y < 2; y++) {
        for (unsigned x = 0; x < 4; x++) {
            g_assert_cmpuint(MIN(255, post_round(expected[56 * y + x], 16)),
                             ==, first_rows[outside][y][x]);
        }
    }
    paired_post_oracle(expected, 56, 28, 0, 0, 0, 3);

    qtest_memread(q, PAIRED_MAIN + (phase % 2) * 0x2000, pixels,
                  sizeof(pixels));
    for (unsigned y = 0; y < 28; y++) {
        for (unsigned x = 0; x < 56; x++) {
            g_assert_cmpuint(pixels[64 * y + x], ==,
                             MIN(255, post_round(expected[56 * y + x], 16)));
        }
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
            qtest_writel(q, PAIRED + 0xfc, batch == 1);
            paired_noise(q, masks[batch], batch);
            if (batch == 1) {
                /* An inactive ring's partial publication cannot block FC1. */
                qtest_writel(q, PAIRED + 0x310,
                             qtest_readl(q, PAIRED + 0x310));
                qtest_writel(q, PAIRED + 0x218,
                             qtest_readl(q, PAIRED + 0x218));
                qtest_writel(q, PAIRED + 0x320,
                             qtest_readl(q, PAIRED + 0x320));
                qtest_writel(q, PAIRED + 0x228,
                             qtest_readl(q, PAIRED + 0x228));
            } else {
                for (unsigned i = 0; i < 4; i++) {
                    qtest_writel(q, PAIRED + 0x310 + 4 * i,
                                 qtest_readl(q, PAIRED + 0x310 + 4 * i));
                    qtest_writel(q, PAIRED + 0x218 + 4 * i,
                                 qtest_readl(q, PAIRED + 0x218 + 4 * i));
                }
            }
            qtest_writel(q, PAIRED + 0x10, 1);
        }
        for (unsigned phase = 0; phase < 4; phase++) {
            paired_raw_input(q);
            pipeline_step(q, 1000000);
            if (batch == 1 && phase == 0) {
                /* The selected ring still needs all four publications. */
                g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"),
                                 ==, 4);
                g_assert_cmphex(qtest_readl(q, PAIRED + 0x24), ==, 0);
                for (unsigned i = 1; i < 4; i++) {
                    qtest_writel(q, PAIRED + 0x320 + 4 * i,
                                 qtest_readl(q, PAIRED + 0x320 + 4 * i));
                    qtest_writel(q, PAIRED + 0x228 + 4 * i,
                                 qtest_readl(q, PAIRED + 0x228 + 4 * i));
                }
                qtest_writel(q, PAIRED + 0x10, 1);
                pipeline_step(q, 2000000);
            }
            g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==,
                             batch * 4 + phase + 1);
            paired_noise_oracle(q, phase, batch == 2, masks[batch]);
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

static void test_paired_noise_update(void)
{
    g_autofree char *bundle = NULL;
    g_autofree char *scene = NULL;
    QTestState *q = paired_start(&bundle, &scene, true, false);

    paired_noise(q, 0xf8200000, 0);
    qtest_writel(q, PAIRED + 0x10, 1);
    qtest_writel(q, PAIRED + 8, 1);
    work_input(q);
    work_rise(q);
    pipeline_step(q, 1000000);
    paired_noise(q, 0x80200003, 0);
    pipeline_step(q, 2000000);
    g_assert_cmpuint(property_at(q, PAIRED_PATH, "cancelled"), ==, 0);
    g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 1);
    paired_noise_oracle(q, 0, false, 0xf8200000);
    qtest_writel(q, PAIRED + 0x24, UINT32_MAX);
    work_ack(q);
    paired_raw_input(q);
    pipeline_step(q, 2000000);
    g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 2);
    paired_noise_oracle(q, 1, true, 0x80200003);
    qtest_qmp_assert_success(q, "{'execute':'system_reset'}");
    pipeline_step(q, 2000000);
    g_assert_cmphex(qtest_readl(q, PAIRED + 0xdf8), ==, 0);
    g_assert_cmphex(qtest_readl(q, PAIRED + 0xe10), ==, 0);
    g_assert_cmphex(qtest_readl(q, PAIRED + 0x24), ==, 0);
    g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 2);
    g_assert_false(qtest_get_irq(q, 99 - 32));
    qtest_quit(q);
    unlink(scene);
    unlink(bundle);
}

static void test_paired_online_gain(void)
{
    g_autofree char *bundle = NULL;
    g_autofree char *scene = NULL;
    QTestState *q = paired_start(&bundle, &scene, true, true);

    work_input(q);
    work_rise(q);
    pipeline_step(q, 1000000);
    g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 0);
    /* The source setter writes R/G, B, then the enable bytes. A partial
     * future packet must neither cancel nor poison the already owned job.
     */
    qtest_writel(q, PAIRED + 0x9d0, 0);
    pipeline_step(q, 2000000);
    g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 1);
    g_assert_cmpuint(property_at(q, PAIRED_PATH, "cancelled"), ==, 0);
    paired_main_oracle(q, 129, 144, 96);
    qtest_writel(q, PAIRED + 0x24, UINT32_MAX);
    work_ack(q);
    paired_raw_input(q);
    pipeline_step(q, 1000000);
    g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 1);
    g_assert_cmphex(qtest_readl(q, PAIRED + 0x24), ==, 0);
    qtest_writel(q, PAIRED + 0x9d4, 0);
    qtest_writel(q, PAIRED + 0x9cc, 0x01010101);
    pipeline_step(q, 2000000);
    g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 2);
    g_assert_cmpuint(property_at(q, PAIRED_PATH, "cancelled"), ==, 0);
    paired_main_at(q, PAIRED_MAIN + 0x2000, 0, 128, 128);
    qtest_quit(q);
    unlink(scene);
    unlink(bundle);
}

static void test_paired_online_boundaries(void)
{
    for (unsigned group = 0; group < 17; group++) {
        unsigned offsets[129];
        unsigned count = paired_online_offsets(group, offsets);

        if (count == 1) {
            continue; /* A singleton has no partial upload. */
        }
        for (unsigned trial = 0; trial < (group < 3 ? count + 3 : 3); trial++) {
            unsigned missing = group < 3 ? trial : count + trial;
            g_autofree char *bundle = NULL;
            g_autofree char *scene = NULL;
            QTestState *q = paired_online_start(&bundle, &scene);
            uint32_t words[129];

            g_test_message("online group=%u missing=%u", group, missing);

            for (unsigned i = 0; i < count; i++) {
                words[i] = qtest_readl(q, PAIRED + offsets[i]);
            }
            work_input(q);
            work_rise(q);
            pipeline_step(q, 1000000);
            if (missing < count) {
                for (unsigned i = 0; i < count; i++) {
                    if (i != missing) {
                        qtest_writel(q, PAIRED + offsets[i], words[i]);
                    }
                }
            } else {
                /* Even malformed staging cannot invalidate an owned snapshot. */
                qtest_writel(q, PAIRED + offsets[0], UINT32_MAX);
            }
            if (missing == count + 1) {
                qtest_writel(q, PAIRED + 0x84, 0);
            }
            pipeline_step(q, 2000000);
            if (missing == count + 1) {
                g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==,
                                 0);
                g_assert_cmpuint(property_at(q, PAIRED_PATH, "cancelled"), ==,
                                 1);
                paired_untouched(q);
                qtest_system_reset(q);
                paired_configure(q, true, PAIRED, 7, 8, 8, 6, 51, 64, 17);
                qtest_writel(q, PAIRED + 0x10, 1);
                qtest_writel(q, PAIRED + 8, 1);
                paired_raw_input(q);
                pipeline_step(q, 2000000);
                g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==,
                                 1);
            } else {
                g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==,
                                 1);
                g_assert_cmpuint(property_at(q, PAIRED_PATH, "cancelled"), ==,
                                 0);
                paired_main_oracle(q, 129, 144, 96);
                qtest_writel(q, PAIRED + 0x24, UINT32_MAX);
                work_ack(q);
                if (missing == count + 2) {
                    qtest_writel(q, PAIRED + 0xc, 1);
                    g_assert_cmphex(qtest_readl(q, PAIRED + 0x18), ==, 0);
                    qtest_writel(q, PAIRED + 0x24, UINT32_MAX);
                }
                paired_raw_input(q);
                pipeline_step(q, 1000000);
                g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==,
                                 1);
                g_assert_cmphex(qtest_readl(q, PAIRED + 0x24), ==, 0);
                /* Start a fresh whole packet; old round coverage cannot help. */
                for (unsigned i = 0; i < count; i++) {
                    qtest_writel(q, PAIRED + offsets[i], words[i]);
                }
                if (missing == count + 2) {
                    qtest_writel(q, PAIRED + 0x10, 1);
                    qtest_writel(q, PAIRED + 8, 1);
                    /* Rearm restarts phase0, whose old generation was already
                     * consumed. Supply real RAW until that ring slot returns.
                     */
                    for (unsigned i = 0; i < 3; i++) {
                        work_ack(q);
                        paired_raw_input(q);
                    }
                }
                pipeline_step(q, 2000000);
                g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==,
                                 2);
                g_assert_cmpuint(property_at(q, PAIRED_PATH, "rejected"), ==,
                                 0);
            }
            qtest_quit(q);
            unlink(scene);
            unlink(bundle);
        }
    }
}

static void paired_filter(QTestState *q, unsigned control, unsigned clip,
                          unsigned retain, unsigned base, unsigned slope)
{
    const unsigned fields[] = { retain, base, 1, slope, 0, 16 };

    qtest_writel(q, PAIRED + 0x594, control);
    for (unsigned channel = 0; channel < 3; channel++) {
        for (unsigned pair = 0; pair < ARRAY_SIZE(fields); pair++) {
            qtest_writel(q, PAIRED + 0x598 + 12 * pair + 4 * channel,
                         (fields[pair] << 16) | fields[pair]);
        }
    }
    qtest_writel(q, PAIRED + 0x608, clip);
}

static QTestState *paired_online_start(char **bundle, char **scene)
{
    QTestState *q = paired_start(bundle, scene, true, false);

    /* Seed the optional families with complete identities before running. */
    paired_filter(q, 0, 1, 16383, 50, 67);
    paired_curve(q, 0, 1024, 129, false);
    paired_curve(q, 1, 1024, 129, false);
    qtest_writel(q, PAIRED + 0x10, 1);
    qtest_writel(q, PAIRED + 8, 1);
    return q;
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
        pipeline_step(q, 1000000);
        if (mode == 5) {
            qtest_writel(q, PAIRED + 0x430, 1);
            qtest_writel(q, PAIRED + 0x470, 3);
        }
        pipeline_step(q, 1000000);
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
            /* Only MAIN Y/UV is written; WORK is disabled and grid is input. */
            g_assert_cmpuint(property_at(q, "/machine/paired-processing",
                                           "bytes"), ==, 56 * (28 + 14));
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

static void test_paired_post(void)
{
    static const uint8_t step[] = {
        'P', '5', '\n', '2', ' ', '1', '\n', '2', '5', '5', '\n', 64, 192,
    };
    static const int c[9] = { 1024, 0, 0, 0, 0, -1024, 0, 1024, 0 };
    g_autofree char *bundle = NULL;
    g_autofree char *scene = NULL;
    QTestState *q = paired_start(&bundle, &scene, true, false);
    uint16_t planes[3][56 * 28];
    uint8_t expected[0xce0], actual[sizeof(expected)];

    g_assert_true(g_file_set_contents(scene, (const char *)step,
                                      sizeof(step), NULL));
    qtest_qmp_assert_success(q,
        "{'execute':'qom-set','arguments':{'path':'/machine/cmos',"
        "'property':'image','value':%s}}", scene);
    paired_matrix(q, PAIRED, 0xb94, c, 13, true);
    qtest_writel(q, PAIRED + 0xb58, (192 << 16) | 96);
    qtest_writel(q, PAIRED + 0xbac, (109 << 16) | 147);
    qtest_writel(q, PAIRED + 0xd28, (17 << 8) | 9);
    qtest_writel(q, PAIRED + 0xdd4, 18);
    qtest_writel(q, PAIRED + 0x10, 1);
    qtest_writel(q, PAIRED + 8, 1);
    paired_raw_input(q);
    pipeline_step(q, 1000000);
    g_assert_cmpuint(property_at(q, WORK_PATH, "completed"), ==, 1);
    g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 1);
    /* Source P5 produces RAW1028/3084. Existing five-tap spatial filtering
     * gives1285/2827 at the two boundary columns (RAW x31/32).
     */
    for (unsigned y = 0; y < 28; y++) {
        for (unsigned x = 0; x < 56; x++) {
            unsigned at = y * 56 + x;
            int s = x < 27 ? 1028 : x == 27 ? 1285 :
                    x == 28 ? 2827 : 3084;
            int green = post_round(s, 2), blue = post_round(s, 4);
            int u = post_round(green - blue, 2);
            int v = post_round(green - s, 2);

            planes[0][at] = s;
            planes[1][at] = CLAMP(post_round(147 * u - 109 * v, 256) +
                                  2048, 0, 4095);
            planes[2][at] = CLAMP(post_round(147 * v + 109 * u, 256) +
                                  2048, 0, 4095);
        }
    }
    for (unsigned channel = 0; channel < 3; channel++) {
        paired_post_oracle(planes[channel], 56, 28, channel ? 96 : 192,
                           17, 9, 18);
    }
    memset(expected, 0xa5, sizeof(expected));
    for (unsigned y = 0; y < 28; y++) {
        for (unsigned x = 0; x < 56; x++) {
            expected[16 + y * 64 + x] = MIN(255,
                post_round(planes[0][y * 56 + x], 16));
        }
    }
    for (unsigned y = 0; y < 14; y++) {
        for (unsigned x = 0; x < 56; x += 2) {
            unsigned at = y * 112 + x;

            for (unsigned channel = 1; channel < 3; channel++) {
                unsigned average = (planes[channel][at] +
                    planes[channel][at + 1] + planes[channel][at + 56] +
                    planes[channel][at + 57] + 2) / 4;

                expected[16 + (28 + y) * 64 + x + channel - 1] =
                    MIN(255, post_round(average, 16));
            }
        }
    }
    qtest_memread(q, PAIRED_MAIN - 16, actual, sizeof(actual));
    g_assert_cmpmem(actual, sizeof(actual), expected, sizeof(expected));
    g_assert_cmphex(qtest_readl(q, PAIRED + 0x24), ==, 0x1002);
    /* WORK retains real pre-color values and padding; grid input is untouched. */
    g_assert_cmphex(qtest_readl(q, PAIRED_WORK), ==, 0x04040404);
    g_assert_cmphex(qtest_readl(q, PAIRED_WORK + 64), ==, 0xa5a5a5a5);
    paired_pixels(q, PAIRED_GRID, 0);
    g_assert_cmphex(qtest_readl(q, PAIRED_GRID + 92), ==, 0xa5a5a5a5);
    qtest_quit(q);
    unlink(scene);
    unlink(bundle);
}

static void test_paired_post_packet(void)
{
    static const uint8_t step[] = {
        'P', '5', '\n', '2', ' ', '1', '\n', '2', '5', '5', '\n', 64, 192,
    };
    static const int identity[] = { 1024, 0, 0, 0, 1024, 0, 0, 0, 1024 };

    for (unsigned test = 0; test < 6; test++) {
        g_autofree char *bundle = NULL;
        g_autofree char *scene = NULL;
        QTestState *q = paired_start(&bundle, &scene, true, false);
        bool gray = test >= 3;
        unsigned family = test % 3;
        PostPacketFixture p = {
            .negative = 30, .positive = 60, .slope = 2, .retain = 4,
            .amount = family == 0 ? 115 : family == 1 ? 360 : 80,
            .modifier = 511, .mix = family == 1 ? 64 : 128,
            .radius = family == 2 ? 3 : 1,
            .diagonal = family == 2 ? 16 : 0,
        };
        uint16_t planes[3][56 * 28];
        uint8_t expected[64 * 42], actual[sizeof(expected)];

        g_assert_true(g_file_set_contents(scene, (const char *)step,
                                          sizeof(step), NULL));
        qtest_qmp_assert_success(q,
            "{'execute':'qom-set','arguments':{'path':'/machine/cmos',"
            "'property':'image','value':%s}}", scene);
        if (gray) {
            paired_matrix(q, PAIRED, 0xb00, identity, 13, false);
        }
        qtest_writel(q, PAIRED + 0xdc0, p.negative << 8 | p.slope);
        qtest_writel(q, PAIRED + 0xdc4, p.positive << 8 | p.slope);
        qtest_writel(q, PAIRED + 0xdc8, p.retain);
        qtest_writel(q, PAIRED + 0xddc, p.amount << 8);
        qtest_writel(q, PAIRED + 0xde0, p.modifier << 8);
        qtest_writel(q, PAIRED + 0xde4, 0x8000 | p.mix);
        qtest_writel(q, PAIRED + 0xde8,
                     0x10000 | p.radius << 8 | p.radius);
        qtest_writel(q, PAIRED + 0xdec, p.diagonal << 8 | p.diagonal);
        qtest_writel(q, PAIRED + 0x10, 1);
        qtest_writel(q, PAIRED + 8, 1);
        paired_raw_input(q);
        pipeline_step(q, 1000000);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 1);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "rejected"), ==, 0);
        for (unsigned y = 0; y < 28; y++) {
            for (unsigned x = 0; x < 56; x++) {
                unsigned at = y * 56 + x;
                int s = x < 27 ? 1028 : x == 27 ? 1285 :
                        x == 28 ? 2827 : 3084;
                int green = gray ? s : post_round(s, 2);
                int blue = gray ? s : post_round(s, 4);

                planes[0][at] = s;
                planes[1][at] = 2048 + post_round(green - blue, 2);
                planes[2][at] = 2048 + post_round(green - s, 2);
            }
        }
        for (unsigned channel = 0; channel < 3; channel++) {
            post_packet_oracle(planes[channel], 56, 28, &p);
            paired_post_oracle(planes[channel], 56, 28, 0, 0, 0, 3);
        }
        memset(expected, 0xa5, sizeof(expected));
        for (unsigned y = 0; y < 28; y++) {
            for (unsigned x = 0; x < 56; x++) {
                expected[y * 64 + x] = MIN(255,
                    post_round(planes[0][y * 56 + x], 16));
            }
        }
        for (unsigned y = 0; y < 14; y++) {
            for (unsigned x = 0; x < 56; x += 2) {
                unsigned at = 2 * y * 56 + x;

                for (unsigned channel = 1; channel < 3; channel++) {
                    unsigned mean = post_round(planes[channel][at] +
                        planes[channel][at + 1] + planes[channel][at + 56] +
                        planes[channel][at + 57], 4);

                    expected[64 * 28 + y * 64 + x + channel - 1] =
                        MIN(255, post_round(mean, 16));
                }
            }
        }
        qtest_memread(q, PAIRED_MAIN, actual, sizeof(actual));
        g_assert_cmpmem(actual, sizeof(actual), expected, sizeof(expected));
        g_assert_cmphex(qtest_readl(q, PAIRED_WORK), ==, 0x04040404);
        g_assert_cmphex(qtest_readl(q, PAIRED + 0x24), ==, 0x1002);
        qtest_quit(q);
        unlink(scene);
        unlink(bundle);
    }
}

static void paired_main_gain(QTestState *q, unsigned table, uint16_t gain,
                              unsigned first, unsigned count)
{
    for (unsigned word = first; word < first + count; word++) {
        qtest_writel(q, PAIRED + 0x1900 + table * 64 + word * 4,
                     gain | ((uint32_t)gain << 16));
    }
}

static void test_paired_main_gain(void)
{
    static const uint16_t gains[] = { 0, 128, 256, 512, 65535 };
    static const uint8_t expected[] = { 0, 64, 129, 255, 255 };

    for (unsigned test = 0; test < ARRAY_SIZE(gains); test++) {
        g_autofree char *bundle = NULL;
        g_autofree char *scene = NULL;
        QTestState *q = paired_start(&bundle, &scene, true, false);

        /* Alternate banks: the latest complete publication wins, not a product
         * of both physical tables or a preference for the higher address.
         */
        if (test == 1) {
            paired_main_gain(q, 0, 512, 0, 16);
        }
        paired_main_gain(q, test % 2, gains[test], 0, 16);
        qtest_writel(q, PAIRED + 0x10, 1);
        qtest_writel(q, PAIRED + 8, 1);
        paired_raw_input(q);
        pipeline_step(q, 1000000);
        g_assert_cmpuint(property_at(q, WORK_PATH, "completed"), ==, 1);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 1);
        paired_main_oracle(q, expected[test], 144, 96);
        paired_pixels(q, PAIRED_WORK, 0);
        paired_pixels(q, PAIRED_GRID, 0);
        g_assert_cmphex(qtest_readl(q, PAIRED + 0x24), ==, 0x1002);
        qtest_quit(q);
        unlink(scene);
        unlink(bundle);
    }
}

static void test_paired_main_gain_spatial(void)
{
    static const uint8_t step[] = {
        'P', '5', '\n', '2', ' ', '1', '\n', '2', '5', '5', '\n', 64, 192,
    };
    g_autofree char *bundle = NULL;
    g_autofree char *scene = NULL;
    QTestState *q = paired_start(&bundle, &scene, true, false);
    uint16_t planes[3][56 * 28];
    uint8_t expected[0xce0], actual[sizeof(expected)];

    g_assert_true(g_file_set_contents(scene, (const char *)step,
                                      sizeof(step), NULL));
    qtest_qmp_assert_success(q,
        "{'execute':'qom-set','arguments':{'path':'/machine/cmos',"
        "'property':'image','value':%s}}", scene);
    paired_main_gain(q, 0, 128, 0, 16);
    qtest_writel(q, PAIRED + 0x10, 1);
    qtest_writel(q, PAIRED + 8, 1);
    paired_raw_input(q);
    pipeline_step(q, 1000000);
    g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 1);
    for (unsigned y = 0; y < 28; y++) {
        for (unsigned x = 0; x < 56; x++) {
            unsigned at = y * 56 + x;
            int s = x < 27 ? 1028 : x == 27 ? 1285 :
                    x == 28 ? 2827 : 3084;
            int green = post_round(s, 2), blue = post_round(s, 4);

            planes[0][at] = s;
            planes[1][at] = 2048 + post_round(green - blue, 2);
            planes[2][at] = 2048 + post_round(green - s, 2);
        }
    }
    for (unsigned channel = 0; channel < 3; channel++) {
        paired_post_oracle(planes[channel], 56, 28, 0, 0, 0, 3);
    }
    memset(expected, 0xa5, sizeof(expected));
    for (unsigned y = 0; y < 28; y++) {
        for (unsigned x = 0; x < 56; x++) {
            unsigned scaled = (planes[0][y * 56 + x] + 1) / 2;

            expected[16 + y * 64 + x] = MIN(255, (scaled + 8) / 16);
        }
    }
    for (unsigned y = 0; y < 14; y++) {
        for (unsigned x = 0; x < 56; x += 2) {
            unsigned at = y * 112 + x;

            for (unsigned channel = 1; channel < 3; channel++) {
                unsigned average = (planes[channel][at] +
                    planes[channel][at + 1] + planes[channel][at + 56] +
                    planes[channel][at + 57] + 2) / 4;

                expected[16 + (28 + y) * 64 + x + channel - 1] =
                    MIN(255, (average + 8) / 16);
            }
        }
    }
    qtest_memread(q, PAIRED_MAIN - 16, actual, sizeof(actual));
    g_assert_cmpmem(actual, sizeof(actual), expected, sizeof(expected));
    g_assert_cmphex(qtest_readl(q, PAIRED + 0x24), ==, 0x1002);
    qtest_quit(q);
    unlink(scene);
    unlink(bundle);
}

static void test_paired_main_gain_boundaries(void)
{
    for (unsigned test = 0; test < 5; test++) {
        g_autofree char *bundle = NULL;
        g_autofree char *scene = NULL;
        QTestState *q = paired_start(&bundle, &scene, test < 2 ? 3 : 1,
                                     false);

        switch (test) {
        case 0: /* Both tables absent must not mean gain0 or identity. */
            break;
        case 1: /* Reset also removes all publication qualification. */
            paired_main_gain(q, 0, 256, 0, 16);
            qtest_system_reset(q);
            paired_configure(q, 3, PAIRED, 7, 8, 8, 6, 51, 64, 17);
            break;
        case 2: /* Old full coverage cannot complete a new15-word round. */
            paired_main_gain(q, 0, 128, 0, 15);
            break;
        case 3: /* Complete but nonuniform: lower and upper halves matter. */
            paired_main_gain(q, 0, 128, 0, 15);
            qtest_writel(q, PAIRED + 0x193c, 0x00810080);
            break;
        case 4: /* A reordered round cannot borrow a prior first word. */
            paired_main_gain(q, 0, 128, 1, 15);
            break;
        }
        qtest_writel(q, PAIRED + 0x10, 1);
        qtest_writel(q, PAIRED + 8, 1);
        paired_raw_input(q);
        pipeline_step(q, 1000000);
        g_assert_cmpuint(property_at(q, WORK_PATH, "completed"), ==, 1);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 0);
        if (test == 2 || test == 4) {
            g_assert_cmpuint(property_at(q, PAIRED_PATH, "rejected"), ==, 0);
        } else {
            g_assert_cmpuint(property_at(q, PAIRED_PATH, "rejected"), >, 0);
        }
        paired_untouched(q);
        if (test == 2) {
            /* Completing publication admits the retained actual RAW without
             * a new edge, injected event, address rewrite or arm request.
             */
            paired_main_gain(q, 0, 128, 15, 1);
            pipeline_step(q, 2000000);
            g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 1);
            g_assert_cmpuint(property_at(q, PAIRED_PATH, "rejected"), ==, 0);
            paired_main_oracle(q, 64, 144, 96);
            paired_pixels(q, PAIRED_WORK, 0);
            paired_pixels(q, PAIRED_GRID, 0);
        }
        qtest_quit(q);
        unlink(scene);
        unlink(bundle);
    }
}

static void test_paired_main_gain_lifecycle(void)
{
    for (unsigned test = 0; test < 4; test++) {
        g_autofree char *bundle = NULL;
        g_autofree char *scene = NULL;
        QTestState *q = paired_start(&bundle, &scene, true, true);

        work_input(q);
        work_rise(q);
        pipeline_step(q, 1000000);
        g_assert_cmpuint(property_at(q, WORK_PATH, "completed"), ==, 1);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 0);
        if (test == 0) {
            /* A partial unselected publication must not reject the old job
             * in its completion-time whole-batch memory preflight.
             */
            paired_main_gain(q, 0, 128, 0, 15);
        } else if (test == 1) {
            paired_main_gain(q, 0, 256, 0, 16);
            paired_main_gain(q, 1, 256, 0, 16);
        } else if (test == 2) {
            paired_main_gain(q, 0, 128, 0, 16);
            /* A completed changed gain belongs to the next actual RAW. */
            g_assert_cmpuint(property_at(q, PAIRED_PATH, "cancelled"), ==, 0);
            paired_untouched(q);
        } else {
            qtest_system_reset(q);
        }
        pipeline_step(q, 2000000);
        if (test == 3) {
            g_assert_cmpuint(property_at(q, PAIRED_PATH, "cancelled"), ==, 1);
            g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 0);
            paired_untouched(q);
        } else {
            g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 1);
            g_assert_cmpuint(property_at(q, PAIRED_PATH, "cancelled"), ==,
                             0);
            paired_main_oracle(q, 129, 144, 96);
            paired_pixels(q, PAIRED_WORK, 0);
            paired_pixels(q, PAIRED_GRID, 0);
        }
        if (test == 0) {
            qtest_writel(q, PAIRED + 0x24, UINT32_MAX);
            work_ack(q);
            paired_raw_input(q);
            pipeline_step(q, 1000000);
            g_assert_cmpuint(property_at(q, WORK_PATH, "completed"), ==, 2);
            g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 1);
            g_assert_cmpuint(property_at(q, PAIRED_PATH, "rejected"), ==, 0);
            g_assert_cmphex(qtest_readl(q, PAIRED + 0x24), ==, 0);
            g_assert_cmphex(qtest_readl(q, PAIRED_MAIN + 0x2000), ==,
                            0xa5a5a5a5);
            /* Only the last missing word publishes and retries actual RAW. */
            paired_main_gain(q, 0, 128, 15, 1);
            pipeline_step(q, 2000000);
            g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 2);
            g_assert_cmpuint(property_at(q, PAIRED_PATH, "rejected"), ==, 0);
            paired_main_at(q, PAIRED_MAIN + 0x2000, 64, 144, 96);
        }
        if (test == 2) {
            qtest_writel(q, PAIRED + 0x24, UINT32_MAX);
            work_ack(q);
            paired_raw_input(q);
            pipeline_step(q, 1000000);
            g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 2);
            paired_main_at(q, PAIRED_MAIN + 0x2000, 64, 144, 96);
        }
        qtest_quit(q);
        unlink(scene);
        unlink(bundle);
    }
}

static void test_paired_post_boundaries(void)
{
    static const unsigned incomplete[] = {
        8, 14, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63,
    };
    static const struct { unsigned offset, value; } invalid[] = {
        { 0xb58, 257 << 16 }, { 0xb58, 257 },
        { 0xbac, 257 }, { 0xbac, 0x00800081 },
        { 0xd28, 0x2000 }, { 0xd28, 0x20 }, { 0xd28, 0x10000 },
        { 0xdd4, 2 }, { 0xdd4, 19 }, { 0xdd8, 4 },
        { 0xb38, 1 }, /* Opaque profile words remain strictly compared. */
        { 0xd24, 1 }, { 0xdc0, 1 }, { 0xbb0, 1 },
        { 0xb94, 512 }, /* C cannot transform luma. */
        { 0xdc0, 0x0900 }, { 0xdc4, 0xc900 },
        { 0xdc0, 0x0a05 }, { 0xdc4, 0x0a01 },
        { 0xdc0, 0x010a00 }, { 0xdc4, 0x010a00 },
        { 0xdc8, 32 }, { 0xdcc, 1 }, { 0xdd0, 0x080400 },
        { 0xddc, 1 }, { 0xddc, 236 << 8 }, { 0xddc, 0x01000000 },
        { 0xde0, 1 }, { 0xde0, 512 << 8 },
        { 0xde4, 0x7f80 }, { 0xde4, 0x803f }, { 0xde4, 0x018080 },
        { 0xde8, 0x010201 }, { 0xde8, 0x010303 }, { 0xde8, 0x020101 },
        { 0xdec, 0x0010 }, { 0xdec, 0x1010 }, { 0xdec, 0x010000 },
    };

    for (unsigned i = 0; i < ARRAY_SIZE(invalid) + ARRAY_SIZE(incomplete); i++) {
        g_autofree char *bundle = NULL;
        g_autofree char *scene = NULL;
        QTestState *q = paired_start(&bundle, &scene, true, false);

        if (i < ARRAY_SIZE(invalid)) {
            qtest_writel(q, PAIRED + invalid[i].offset, invalid[i].value);
        } else {
            qtest_system_reset(q);
            /* Reset clears coverage even for retained-value zero words. */
            paired_configure(q, true, PAIRED, 7, 8, 8, 6, 51,
                             incomplete[i - ARRAY_SIZE(invalid)], 17);
        }
        qtest_writel(q, PAIRED + 0x10, 1);
        qtest_writel(q, PAIRED + 8, 1);
        paired_raw_input(q);
        pipeline_step(q, 1000000);
        g_assert_cmpuint(property_at(q, WORK_PATH, "completed"), ==, 1);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 0);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "rejected"), >, 0);
        paired_untouched(q);
        g_assert_cmphex(qtest_readl(q, PAIRED + 0x24), ==, 0);
        qtest_quit(q);
        unlink(scene);
        unlink(bundle);
    }
}

static void test_paired_post_lifecycle(void)
{
    static const struct { unsigned offset, value; } changes[] = {
        { 0xb58, 51 << 16 }, { 0xbac, 0x006d0093 },
        { 0xd28, 0x300 }, { 0xdd4, 4 },
        { 0xdc0, 0x1400 }, { 0xdc8, 31 }, { 0xddc, 115 << 8 },
        { 0xde0, 511 << 8 }, { 0xde4, 0x8040 },
    };

    for (unsigned test = 0; test < ARRAY_SIZE(changes) + 2; test++) {
        g_autofree char *bundle = NULL;
        g_autofree char *scene = NULL;
        QTestState *q = paired_start(&bundle, &scene, true, true);

        work_input(q);
        work_rise(q);
        pipeline_step(q, 1000000);
        g_assert_cmpuint(property_at(q, WORK_PATH, "completed"), ==, 1);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 0);
        if (test < ARRAY_SIZE(changes)) {
            paired_online_republish(q, changes[test].offset, changes[test].value);
        } else if (test == ARRAY_SIZE(changes)) {
            qtest_system_reset(q);
        } else {
            const unsigned ranges[][2] = {
                { 0xb38, 0xb58 }, { 0xb94, 0xbbc },
                { 0xd20, 0xd9c }, { 0xdc0, 0xdec },
            };

            /* Do not rewrite C through temporary zero coefficients: equal
             * covered words alone must preserve the immutable active job.
             */
            for (unsigned group = 0; group < ARRAY_SIZE(ranges); group++) {
                for (unsigned offset = ranges[group][0];
                     offset <= ranges[group][1]; offset += 4) {
                    qtest_writel(q, PAIRED + offset,
                                 qtest_readl(q, PAIRED + offset));
                }
            }
        }
        pipeline_step(q, 2000000);
        if (test == ARRAY_SIZE(changes)) {
            g_assert_cmpuint(property_at(q, PAIRED_PATH, "cancelled"), ==, 1);
            g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 0);
            paired_untouched(q);
        } else {
            g_assert_cmpuint(property_at(q, PAIRED_PATH, "cancelled"), ==, 0);
            g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 1);
            paired_pixels(q, PAIRED_MAIN, 0);
        }
        if (test < ARRAY_SIZE(changes)) {
            qtest_writel(q, PAIRED + 0x24, UINT32_MAX);
            work_ack(q);
            paired_raw_input(q);
            pipeline_step(q, 1000000);
            g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 2);
            g_assert_cmpuint(property_at(q, PAIRED_PATH, "rejected"), ==, 0);
            paired_pixels(q, PAIRED_MAIN, 1);
        }
        qtest_quit(q);
        unlink(scene);
        unlink(bundle);
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
        paired_raw_input(q);
        pipeline_step(q, 1000000);
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
        paired_raw_input(q);
        pipeline_step(q, 1000000);
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
        pipeline_step(q, 1000000);
        paired_filter(q, 0, 1, mode == 1 ? 6762 : 6087, 50, 67);
        if (mode == 2) {
            qtest_qmp_assert_success(q, "{'execute':'system_reset'}");
        }
        pipeline_step(q, 2000000);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==,
                         mode != 2);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "cancelled"), ==,
                         mode == 2);
        if (mode != 2) {
            paired_pixels(q, PAIRED_MAIN, 0);
            paired_pixels(q, PAIRED_WORK, 0);
            paired_pixels(q, PAIRED_GRID, 0);
        } else {
            paired_untouched(q);
        }
        if (mode == 1) {
            qtest_writel(q, PAIRED + 0x24, UINT32_MAX);
            work_ack(q);
            paired_raw_input(q);
            pipeline_step(q, 1000000);
            g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 2);
            g_assert_cmpuint(property_at(q, PAIRED_PATH, "rejected"), ==, 0);
            paired_pixels(q, PAIRED_MAIN, 1);
            paired_pixels(q, PAIRED_WORK, 1);
            paired_pixels(q, PAIRED_GRID, 1);
        }
        qtest_quit(q);
        unlink(scene);
        unlink(bundle);
    }
}

static void test_paired_denoise(void)
{
    static const uint8_t step[] = {
        'P', '5', '\n', '2', ' ', '1', '\n', '2', '5', '5', '\n', 128, 130,
    };
    static const uint8_t edge[] = { 129, 129, 130, 130 };
    g_autofree char *bundle = NULL;
    g_autofree char *scene = NULL;
    uint8_t pixels[64 * 28];
    QTestState *q = paired_start(&bundle, &scene, true, false);

    g_assert_true(g_file_set_contents(scene, (const char *)step,
                                      sizeof(step), NULL));
    qtest_qmp_assert_success(q,
        "{'execute':'qom-set','arguments':{'path':'/machine/cmos',"
        "'property':'image','value':%s}}", scene);
    paired_denoise(q, PAIRED, 103, 43, 17, 10, 12, 8, 7);
    qtest_writel(q, PAIRED + 0x10, 1);
    qtest_writel(q, PAIRED + 8, 1);
    paired_raw_input(q);
    pipeline_step(q, 1000000);
    g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 1);
    qtest_memread(q, PAIRED_MAIN, pixels, sizeof(pixels));
    for (unsigned row = 0; row < 28; row++) {
        for (unsigned x = 0; x < 56; x++) {
            unsigned expected = x < 26 ? 129 : x >= 30 ? 131 : edge[x - 26];

            g_assert_cmpuint(pixels[64 * row + x], ==, expected);
        }
        for (unsigned x = 56; x < 64; x++) {
            g_assert_cmphex(pixels[64 * row + x], ==, 0xa5);
        }
    }
    /* Before NR, the two half means are2058/2086; NR computes2061/2083. */
    for (unsigned row = 0; row < 18; row++) {
        g_assert_cmpuint(qtest_readw(q, PAIRED_WORK + row * 80 + 15 * 2),
                         ==, 2061);
        g_assert_cmpuint(qtest_readw(q, PAIRED_WORK + row * 80 + 16 * 2),
                         ==, 2083);
        g_assert_cmpuint(qtest_readw(q, PAIRED_WORK + (row + 18) * 80 + 15 * 2),
                         ==, 0);
    }
    g_assert_cmphex(qtest_readl(q, PAIRED + 0x24), ==, 0x1002);
    qtest_writel(q, PAIRED + 0xe8, 0);
    g_assert_true(qtest_get_irq(q, 99 - 32));
    qtest_writel(q, PAIRED + 0x24, UINT32_MAX);
    g_assert_false(qtest_get_irq(q, 99 - 32));
    qtest_quit(q);
    unlink(scene);
    unlink(bundle);
}

static void test_paired_denoise_boundaries(void)
{
    static const struct { unsigned offset; uint32_t value; } invalid[] = {
        { 0x660, 0 }, { 0x660, 6 }, { 0x660, 104 }, { 0x660, 0x10007 },
        { 0x664, 0x00010002 }, { 0x664, 0xf0010003 },
        { 0x668, 0x02010000 }, { 0x668, 0x12010000 },
        { 0x668, 0x03000000 }, { 0x668, 0x03040000 },
        { 0x668, 0x03010100 }, { 0x668, 0x03010d0d },
        { 0x66c, 0x07070707 }, { 0x66c, 0x20202020 },
        { 0x66c, 0x1f1f1f1e }, { 0x670, 1 },
        { 0x674, 0x02030001 }, { 0x674, 0x03020101 },
        { 0x678, 1 }, { 0x678, 0x10000 }, { 0x678, 0x100 },
    };

    for (unsigned mode = 0; mode < ARRAY_SIZE(invalid) + 2; mode++) {
        g_autofree char *bundle = NULL;
        g_autofree char *scene = NULL;
        QTestState *q = paired_start(&bundle, &scene, true, false);

        if (mode < ARRAY_SIZE(invalid)) {
            qtest_writel(q, PAIRED + invalid[mode].offset, invalid[mode].value);
        } else {
            /* Reset must forget all seven publication bits, including zeros. */
            qtest_system_reset(q);
            paired_configure(q, true, PAIRED,
                             mode == ARRAY_SIZE(invalid) ? 0 : 6,
                             8, 8, 6, 51, 64, 17);
        }
        qtest_writel(q, PAIRED + 0x10, 1);
        qtest_writel(q, PAIRED + 8, 1);
        paired_raw_input(q);
        pipeline_step(q, 1000000);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 0);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "rejected"), >, 0);
        paired_untouched(q);
        qtest_quit(q);
        unlink(scene);
        unlink(bundle);
    }
}

static void test_paired_denoise_lifecycle(void)
{
    for (unsigned mode = 0; mode < 3; mode++) {
        g_autofree char *bundle = NULL;
        g_autofree char *scene = NULL;
        QTestState *q = paired_start(&bundle, &scene, true, false);

        paired_denoise(q, PAIRED, 103, 43, 17, 10, 12, 8, 7);
        qtest_writel(q, PAIRED + 0x10, 1);
        qtest_writel(q, PAIRED + 8, 1);
        work_input(q);
        work_rise(q);
        pipeline_step(q, 1000000);
        if (mode == 2) {
            qtest_system_reset(q);
            for (unsigned offset = 0x660; offset <= 0x678; offset += 4) {
                g_assert_cmphex(qtest_readl(q, PAIRED + offset), ==, 0);
            }
        } else {
            paired_denoise(q, PAIRED, 103, 43, 17, 10, 12,
                            mode == 1 ? 9 : 8, 7);
        }
        pipeline_step(q, 2000000);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==,
                         mode != 2);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "cancelled"), ==,
                         mode == 2);
        if (mode != 2) {
            paired_pixels(q, PAIRED_MAIN, 0);
            paired_pixels(q, PAIRED_WORK, 0);
            paired_pixels(q, PAIRED_GRID, 0);
        } else {
            paired_untouched(q);
        }
        if (mode == 1) {
            qtest_writel(q, PAIRED + 0x24, UINT32_MAX);
            work_ack(q);
            paired_raw_input(q);
            pipeline_step(q, 1000000);
            g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 2);
            g_assert_cmpuint(property_at(q, PAIRED_PATH, "rejected"), ==, 0);
            paired_pixels(q, PAIRED_MAIN, 1);
            paired_pixels(q, PAIRED_WORK, 1);
            paired_pixels(q, PAIRED_GRID, 1);
        }
        qtest_quit(q);
        unlink(scene);
        unlink(bundle);
    }
}

static void test_paired_detail(void)
{
    static const char header[] = "P5\n64 36\n255\n";
    const unsigned prefix = sizeof(header) - 1;

    for (unsigned sign = 0; sign < 2; sign++) {
        g_autofree char *bundle = NULL;
        g_autofree char *scene = NULL;
        g_autofree char *image = g_malloc(prefix + 64 * 36);
        uint8_t pixels[64 * 28];
        QTestState *q = paired_start(&bundle, &scene, true, false);

        memcpy(image, header, prefix);
        memset(image + prefix, sign ? 64 : 128, 64 * 36);
        image[prefix + 17 * 64 + 31] = (char)(sign ? 62 : 130);
        g_assert_true(g_file_set_contents(scene, image, prefix + 64 * 36,
                                          NULL));
        qtest_qmp_assert_success(q,
            "{'execute':'qom-set','arguments':{'path':'/machine/cmos',"
            "'property':'image','value':%s}}", scene);
        paired_detail(q, PAIRED, 31, 32, 16, 8);
        qtest_writel(q, PAIRED + 0x10, 1);
        qtest_writel(q, PAIRED + 8, 1);
        paired_raw_input(q);
        pipeline_step(q, 1000000);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 1);
        qtest_memread(q, PAIRED_MAIN, pixels, sizeof(pixels));
        for (unsigned row = 0; row < 28; row++) {
            for (unsigned x = 0; x < 56; x++) {
                g_assert_cmpuint(pixels[64 * row + x], ==, sign ? 64 : 129);
            }
            for (unsigned x = 56; x < 64; x++) {
                g_assert_cmphex(pixels[64 * row + x], ==, 0xa5);
            }
        }
        /* The source impulse gives MAIN130/63 without detail, versus129/64. */
        g_assert_cmpuint(qtest_readw(q, PAIRED_WORK + 8 * 80 + 15 * 2),
                         ==, sign ? 1024 : 2060);
        g_assert_cmpuint(qtest_readw(q, PAIRED_WORK + (8 + 18) * 80 + 15 * 2),
                         ==, 0);
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

static void test_paired_adaptive(void)
{
    static const char header[] = "P5\n64 36\n255\n";
    const unsigned prefix = sizeof(header) - 1;

    for (unsigned sign = 0; sign < 2; sign++) {
        g_autofree char *bundle = NULL;
        g_autofree char *scene = NULL;
        g_autofree char *image = g_malloc(prefix + 64 * 36);
        uint8_t pixels[64 * 28];
        QTestState *q = paired_start(&bundle, &scene, true, false);

        memcpy(image, header, prefix);
        memset(image + prefix, sign ? 64 : 128, 64 * 36);
        image[prefix + 17 * 64 + 31] = (char)(sign ? 62 : 130);
        g_assert_true(g_file_set_contents(scene, image, prefix + 64 * 36,
                                          NULL));
        qtest_qmp_assert_success(q,
            "{'execute':'qom-set','arguments':{'path':'/machine/cmos',"
            "'property':'image','value':%s}}", scene);
        paired_adaptive(q, PAIRED, 15, 8);
        qtest_writel(q, PAIRED + 0x10, 1);
        qtest_writel(q, PAIRED + 8, 1);
        paired_raw_input(q);
        pipeline_step(q, 1000000);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 1);
        qtest_memread(q, PAIRED_MAIN, pixels, sizeof(pixels));
        for (unsigned row = 0; row < 28; row++) {
            for (unsigned x = 0; x < 56; x++) {
                g_assert_cmpuint(pixels[64 * row + x], ==, sign ? 64 : 129);
            }
            for (unsigned x = 56; x < 64; x++) {
                g_assert_cmphex(pixels[64 * row + x], ==, 0xa5);
            }
        }
        /* Pre-suffix impulse is2072/1012; exact suffix gives2063/1021.
         * The2x2 block at(30,16) averages2060/1024 in WORK.
         */
        g_assert_cmpuint(qtest_readw(q, PAIRED_WORK + 8 * 80 + 15 * 2),
                         ==, sign ? 1024 : 2060);
        g_assert_cmpuint(qtest_readw(q, PAIRED_WORK + 26 * 80 + 15 * 2),
                         ==, 0);
        paired_pixels(q, PAIRED_GRID, 0);
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

static void test_paired_adaptive_boundaries(void)
{
    static const struct { unsigned offset; uint32_t value; } invalid[] = {
        { 0x75c, 0x0032001d }, { 0x75c, 0x0031001e },
        { 0x75c, 0x012d001e }, { 0x760, 0x003b0fff },
        { 0x760, 0x10000fff }, { 0x760, 0x003c000f },
        { 0x760, 0x003c1000 }, { 0x764, 0x00630fff },
        { 0x764, 0x04010fff }, { 0x764, 0x04000ffe },
        { 0x768, 0x071f0c0e }, { 0x768, 0x0b1f0c0e },
        { 0x768, 0x081e0c0e }, { 0x768, 0x081f0d0e },
        { 0x768, 0x081f0c0f }, { 0x76c, 0x03010000 },
        { 0x76c, 0x04100000 }, { 0x76c, 0x04010100 },
        { 0x76c, 0x04010001 }, { 0x770, 0x08080809 },
        { 0x774, 0x00010808 }, { 0x778, 0x0af00021 },
    };

    for (unsigned mode = 0; mode < ARRAY_SIZE(invalid) + 2; mode++) {
        g_autofree char *bundle = NULL;
        g_autofree char *scene = NULL;
        QTestState *q = paired_start(&bundle, &scene, true, false);

        if (mode < ARRAY_SIZE(invalid)) {
            qtest_writel(q, PAIRED + invalid[mode].offset, invalid[mode].value);
        } else {
            qtest_system_reset(q);
            paired_configure(q, true, PAIRED, 7, 8,
                             mode == ARRAY_SIZE(invalid) ? 0 : 7, 6, 51, 64, 17);
        }
        qtest_writel(q, PAIRED + 0x10, 1);
        qtest_writel(q, PAIRED + 8, 1);
        paired_raw_input(q);
        pipeline_step(q, 1000000);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 0);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "rejected"), >, 0);
        paired_untouched(q);
        qtest_quit(q);
        unlink(scene);
        unlink(bundle);
    }
}

static void test_paired_adaptive_lifecycle(void)
{
    for (unsigned mode = 0; mode < 3; mode++) {
        g_autofree char *bundle = NULL;
        g_autofree char *scene = NULL;
        QTestState *q = paired_start(&bundle, &scene, true, false);

        paired_adaptive(q, PAIRED, 4, 8);
        qtest_writel(q, PAIRED + 0x10, 1);
        qtest_writel(q, PAIRED + 8, 1);
        work_input(q);
        work_rise(q);
        pipeline_step(q, 1000000);
        if (mode == 2) {
            qtest_system_reset(q);
            for (unsigned offset = 0x75c; offset <= 0x778; offset += 4) {
                g_assert_cmphex(qtest_readl(q, PAIRED + offset), ==, 0);
            }
        } else {
            paired_adaptive(q, PAIRED, mode == 1 ? 5 : 4, 8);
        }
        pipeline_step(q, 2000000);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==,
                         mode != 2);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "cancelled"), ==,
                         mode == 2);
        if (mode != 2) {
            paired_pixels(q, PAIRED_MAIN, 0);
            paired_pixels(q, PAIRED_WORK, 0);
            paired_pixels(q, PAIRED_GRID, 0);
        } else {
            paired_untouched(q);
            g_assert_cmphex(qtest_readl(q, PAIRED + 0x24), ==, 0);
        }
        if (mode == 1) {
            qtest_writel(q, PAIRED + 0x24, UINT32_MAX);
            work_ack(q);
            paired_raw_input(q);
            pipeline_step(q, 1000000);
            g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 2);
            g_assert_cmpuint(property_at(q, PAIRED_PATH, "rejected"), ==, 0);
            paired_pixels(q, PAIRED_MAIN, 1);
            paired_pixels(q, PAIRED_WORK, 1);
            paired_pixels(q, PAIRED_GRID, 1);
        }
        qtest_quit(q);
        unlink(scene);
        unlink(bundle);
    }
}

static void test_paired_adaptive_history(void)
{
    static const uint8_t gray[] = {
        'P', '5', '\n', '1', ' ', '1', '\n', '2', '5', '5', '\n', 129,
    };

    for (unsigned changed = 0; changed < 2; changed++) {
        g_autofree char *bundle = NULL;
        g_autofree char *scene = NULL;
        QTestState *q = paired_start(&bundle, &scene, true, true);

        for (unsigned phase = 0; phase < 2; phase++) {
            paired_raw_input(q);
            pipeline_step(q, 1000000);
            g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==,
                             phase + 1);
            qtest_writel(q, PAIRED + 0x24, UINT32_MAX);
            work_ack(q);
        }
        paired_online_republish(q, 0x76c, 0x04000000 | (changed << 16));
        g_assert_true(g_file_set_contents(scene, (const char *)gray,
                                          sizeof(gray), NULL));
        qtest_qmp_assert_success(q,
            "{'execute':'qom-set','arguments':{'path':'/machine/cmos',"
            "'property':'image','value':%s}}", scene);
        paired_raw_input(q);
        pipeline_step(q, 1000000);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 3);
        g_assert_cmphex(qtest_readl(q, WORK_BUFFER + 0x3000), ==, 0x18818818);
        /* Uniform pixels make dose1 numerically neutral. Only a real config
         * change invalidates history; an equal write preserves the old mean.
         */
        g_assert_cmpuint(qtest_readw(q, PAIRED_WORK), ==,
                         changed ? 2072 : 2068);
        g_assert_cmpuint(qtest_readw(q, PAIRED_WORK + 18 * 80), ==,
                         changed ? 0 : 16);
        g_assert_cmphex(qtest_readl(q, PAIRED + 0x24), ==, 0x1002);
        qtest_quit(q);
        unlink(scene);
        unlink(bundle);
    }
}

static void test_paired_detail_boundaries(void)
{
    static const struct { unsigned offset; uint32_t value; } invalid[] = {
        { 0x73c, 0x00060300 }, { 0x73c, 0x01060301 }, { 0x740, 1 },
        { 0x744, 0 }, { 0x744, 0x40030600 }, { 0x744, 0x40200600 },
        { 0x744, 0x40040621 }, { 0x744, 0x41040600 },
        { 0x744, 0x40040700 }, { 0x748, 0x04080809 },
        { 0x748, 0x03080808 }, { 0x748, 0x04070707 },
        { 0x748, 0x04111111 }, { 0x74c, 0x00040011 },
        { 0x74c, 0x00050010 }, { 0x74c, 0xff040010 }, { 0x750, 1 },
        { 0x754, 0x001e0000 }, { 0x754, 0x001f0001 },
        { 0x758, 0x1f100805 },
    };

    for (unsigned mode = 0; mode < ARRAY_SIZE(invalid) + 2; mode++) {
        g_autofree char *bundle = NULL;
        g_autofree char *scene = NULL;
        QTestState *q = paired_start(&bundle, &scene, true, false);

        if (mode < ARRAY_SIZE(invalid)) {
            qtest_writel(q, PAIRED + invalid[mode].offset, invalid[mode].value);
        } else {
            qtest_system_reset(q);
            paired_configure(q, true, PAIRED, 7,
                             mode == ARRAY_SIZE(invalid) ? 0 : 7,
                             8, 6, 51, 64, 17);
        }
        qtest_writel(q, PAIRED + 0x10, 1);
        qtest_writel(q, PAIRED + 8, 1);
        paired_raw_input(q);
        pipeline_step(q, 1000000);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 0);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "rejected"), >, 0);
        paired_untouched(q);
        qtest_quit(q);
        unlink(scene);
        unlink(bundle);
    }
}

static void test_paired_detail_lifecycle(void)
{
    for (unsigned mode = 0; mode < 3; mode++) {
        g_autofree char *bundle = NULL;
        g_autofree char *scene = NULL;
        QTestState *q = paired_start(&bundle, &scene, true, false);

        paired_detail(q, PAIRED, 31, 32, 16, 8);
        qtest_writel(q, PAIRED + 0x10, 1);
        qtest_writel(q, PAIRED + 8, 1);
        work_input(q);
        work_rise(q);
        pipeline_step(q, 1000000);
        if (mode == 2) {
            qtest_system_reset(q);
            for (unsigned offset = 0x73c; offset <= 0x758; offset += 4) {
                g_assert_cmphex(qtest_readl(q, PAIRED + offset), ==, 0);
            }
        } else {
            paired_detail(q, PAIRED, 31, mode == 1 ? 31 : 32, 16, 8);
        }
        pipeline_step(q, 2000000);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==,
                         mode != 2);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "cancelled"), ==,
                         mode == 2);
        if (mode != 2) {
            paired_pixels(q, PAIRED_MAIN, 0);
            paired_pixels(q, PAIRED_WORK, 0);
            paired_pixels(q, PAIRED_GRID, 0);
        } else {
            paired_untouched(q);
        }
        if (mode == 1) {
            qtest_writel(q, PAIRED + 0x24, UINT32_MAX);
            work_ack(q);
            paired_raw_input(q);
            pipeline_step(q, 1000000);
            g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 2);
            g_assert_cmpuint(property_at(q, PAIRED_PATH, "rejected"), ==, 0);
            paired_pixels(q, PAIRED_MAIN, 1);
            paired_pixels(q, PAIRED_WORK, 1);
            paired_pixels(q, PAIRED_GRID, 1);
        }
        qtest_quit(q);
        unlink(scene);
        unlink(bundle);
    }
}

static unsigned saturation_gradient(unsigned color, unsigned x)
{
    static const unsigned base[] = { 16, 64, 96 };

    return ((base[color] + x) * 257) >> 4;
}

static void test_paired_saturation(void)
{
    static const char header[] = "P6\n64 72\n255\n";
    static const int identity[] = { 1024, 0, 0, 0, 1024, 0, 0, 0, 1024 };

    for (unsigned mode = 0; mode < 5; mode++) {
        g_autofree char *bundle = NULL;
        g_autofree char *scene = NULL;
        QTestState *q = paired_start(&bundle, &scene, true, false);
        uint8_t ppm[sizeof(header) - 1 + 64 * 72 * 3];
        uint8_t expected[64 * 42], actual[sizeof(expected)];
        uint16_t planes[3][56 * 28], knots[33];
        unsigned first = mode == 3 ? UINT16_MAX : mode == 4 ? 0 : 128;
        unsigned last = mode == 3 ? UINT16_MAX : mode == 4 ? 0 : 384;

        if (mode == 1) {
            qtest_system_reset(q);
            paired_configure(q, true, PAIRED, 7, 8, 8, 6, 51, 64, 0);
        }
        paired_saturation(q, PAIRED, mode == 1, first, last, 17);
        if (mode == 2) {
            paired_saturation(q, PAIRED, 1, first, last, 17);
        }
        for (unsigned i = 0; i < 33; i++) {
            knots[i] = ((32 - i) * first + i * last + 16) / 32;
        }
        memcpy(ppm, header, sizeof(header) - 1);
        for (unsigned y = 0; y < 72; y++) {
            for (unsigned x = 0; x < 64; x++) {
                unsigned at = sizeof(header) - 1 + 3 * (y * 64 + x);

                ppm[at] = 16 + x;
                ppm[at + 1] = 64 + x;
                ppm[at + 2] = 96 + x;
            }
        }
        g_assert_true(g_file_set_contents(scene, (const char *)ppm,
                                          sizeof(ppm), NULL));
        qtest_qmp_assert_success(q,
            "{'execute':'qom-set','arguments':{'path':'/machine/cmos',"
            "'property':'image','value':%s}}", scene);
        paired_matrix(q, PAIRED, 0xb00, identity, 13, false);
        qtest_writel(q, PAIRED + 0x10, 1);
        qtest_writel(q, PAIRED + 8, 1);
        paired_cfa_input(q);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 1);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "rejected"), ==, 0);
        /* A linear, row-constant RGB scene survives same-phase spatial passes.
         * Derive the four CFA measurements and bilinear missing colors here.
         */
        for (unsigned y = 0; y < 28; y++) {
            for (unsigned x = 0; x < 56; x++) {
                unsigned sx = x + 4, sy = y + 4, at = y * 56 + x;
                int r = sx & 1 ? (saturation_gradient(0, sx - 1) +
                    saturation_gradient(0, sx + 1) + 1) / 2 :
                    saturation_gradient(0, sx);
                int b = sx & 1 ? saturation_gradient(2, sx) :
                    (saturation_gradient(2, sx - 1) +
                     saturation_gradient(2, sx + 1) + 1) / 2;
                int g = saturation_gradient(1, sx);
                unsigned index = r / 128, fraction = r % 128;
                unsigned gain = post_round(knots[index] * (128 - fraction) +
                                           knots[index + 1] * fraction, 128);

                if (((sx ^ sy) & 1) == 0) {
                    int other = (saturation_gradient(1, sx - 1) +
                                 saturation_gradient(1, sx + 1) + 1) / 2;

                    g = (g + other + 1) / 2;
                }
                planes[0][at] = r;
                planes[1][at] = CLAMP(post_round(
                    post_round(g - b, 2) * gain, 256) + 2048, 0, 4095);
                planes[2][at] = CLAMP(post_round(
                    post_round(g - r, 2) * gain, 256) + 2048, 0, 4095);
            }
        }
        for (unsigned channel = 0; channel < 3; channel++) {
            paired_post_oracle(planes[channel], 56, 28, 0, 0, 0, 3);
        }
        memset(expected, 0xa5, sizeof(expected));
        for (unsigned y = 0; y < 28; y++) {
            for (unsigned x = 0; x < 56; x++) {
                expected[y * 64 + x] = MIN(255,
                    post_round(planes[0][y * 56 + x], 16));
            }
        }
        for (unsigned y = 0; y < 14; y++) {
            for (unsigned x = 0; x < 56; x += 2) {
                unsigned at = 2 * y * 56 + x;

                for (unsigned channel = 1; channel < 3; channel++) {
                    unsigned mean = post_round(planes[channel][at] +
                        planes[channel][at + 1] + planes[channel][at + 56] +
                        planes[channel][at + 57], 4);

                    expected[64 * 28 + y * 64 + x + channel - 1] =
                        MIN(255, post_round(mean, 16));
                }
            }
        }
        qtest_memread(q, PAIRED_MAIN, actual, sizeof(actual));
        g_assert_cmpmem(actual, sizeof(actual), expected, sizeof(expected));
        g_assert_cmpuint(qtest_readw(q, PAIRED_WORK + 8 * 80 + 15 * 2), ==,
            post_round(saturation_gradient(0, 30) +
                saturation_gradient(1, 31) + saturation_gradient(1, 30) +
                saturation_gradient(2, 31), 4));
        qtest_quit(q);
        unlink(scene);
        unlink(bundle);
    }
}

static void test_paired_saturation_boundaries(void)
{
    for (unsigned mode = 0; mode < 39; mode++) {
        g_autofree char *bundle = NULL;
        g_autofree char *scene = NULL;
        QTestState *q = paired_start(&bundle, &scene, true, false);

        if (mode < 17) {
            qtest_system_reset(q);
            paired_configure(q, true, PAIRED, 7, 8, 8, 6, 51, 64, mode);
        } else if (mode < 34) {
            /* A new round cannot borrow even one word from the old full mask. */
            unsigned missing = mode - 17;

            for (unsigned word = 0; word < 17; word++) {
                if (word != missing) {
                    qtest_writel(q, PAIRED + 0x1a08 + 4 * word,
                                 word == 16 ? 256 : 0x01000100);
                }
            }
        } else if (mode == 34) {
            paired_saturation(q, PAIRED, 1, 128, 128, 17);
        } else if (mode == 35) {
            paired_saturation(q, PAIRED, 1, 256, 256, 16);
        } else if (mode == 36) {
            paired_saturation(q, PAIRED, 0, 256, 256, 16);
            qtest_writel(q, PAIRED + 0x1a48, 0x00010100);
        } else if (mode == 37) {
            paired_saturation(q, PAIRED, 1, 256, 256, 16);
            qtest_writel(q, PAIRED + 0x1a8c, 0x00010100);
        } else {
            paired_saturation(q, PAIRED, 0, 256, 256, 16);
            /* Duplicate/out-of-order non-header writes invalidate this round. */
            qtest_writel(q, PAIRED + 0x1a0c, 0x01000100);
            qtest_writel(q, PAIRED + 0x1a48, 256);
        }
        qtest_writel(q, PAIRED + 0x10, 1);
        qtest_writel(q, PAIRED + 8, 1);
        paired_raw_input(q);
        pipeline_step(q, 1000000);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 0);
        if (mode == 33 || mode == 35) {
            g_assert_cmpuint(property_at(q, PAIRED_PATH, "rejected"), ==, 0);
            g_assert_cmpuint(property_at(q, PAIRED_PATH, "blocked"), >, 0);
        } else {
            g_assert_cmpuint(property_at(q, PAIRED_PATH, "rejected"), >, 0);
        }
        paired_untouched(q);
        qtest_quit(q);
        unlink(scene);
        unlink(bundle);
    }
}

static void test_paired_saturation_lifecycle(void)
{
    for (unsigned mode = 0; mode < 6; mode++) {
        g_autofree char *bundle = NULL;
        g_autofree char *scene = NULL;
        QTestState *q = paired_start(&bundle, &scene, true, true);

        work_input(q);
        work_rise(q);
        pipeline_step(q, 1000000);
        if (mode == 5) {
            qtest_system_reset(q);
            for (unsigned offset = 0x1a08; offset <= 0x1a8c; offset += 4) {
                g_assert_cmphex(qtest_readl(q, PAIRED + offset), ==, 0);
            }
        } else if (mode == 4) {
            paired_saturation(q, PAIRED, 1, 128, 128, 17);
        } else {
            /* Every partial/full future curve preserves the owned old image. */
            paired_saturation(q, PAIRED, 0, mode >= 2 ? 128 : 256,
                               mode >= 2 ? 128 : 256, mode & 1 ? 17 : 16);
        }
        pipeline_step(q, 2000000);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==,
                         mode != 5);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "cancelled"), ==,
                         mode == 5);
        if (mode == 5) {
            paired_untouched(q);
        } else {
            paired_pixels(q, PAIRED_MAIN, 0);
            paired_pixels(q, PAIRED_WORK, 0);
            paired_pixels(q, PAIRED_GRID, 0);
        }
        if (mode == 3) {
            qtest_writel(q, PAIRED + 0x24, UINT32_MAX);
            work_ack(q);
            paired_raw_input(q);
            pipeline_step(q, 1000000);
            g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 2);
            paired_main_at(q, PAIRED_MAIN + 0x2000, 129, 136, 112);
        }
        qtest_quit(q);
        unlink(scene);
        unlink(bundle);
    }
}

static void test_paired_saturation_staging(void)
{
    for (unsigned armed = 0; armed < 2; armed++) {
        g_autofree char *bundle = NULL;
        g_autofree char *scene = NULL;
        QTestState *q = paired_start(&bundle, &scene, true, armed);

        paired_saturation(q, PAIRED, 0, 128, 128, 16);
        paired_raw_input(q);
        pipeline_step(q, 1000000);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 0);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "rejected"), ==, 0);
        if (armed) {
            g_assert_cmpuint(property_at(q, PAIRED_PATH, "blocked"), >, 0);
        }
        qtest_writel(q, PAIRED + 0x1a48, 128);
        pipeline_step(q, 2000000);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, armed);
        if (!armed) {
            /* Publication cannot create a request or implicitly arm processing. */
            paired_untouched(q);
            qtest_writel(q, PAIRED + 0x10, 1);
            qtest_writel(q, PAIRED + 8, 1);
            pipeline_step(q, 2000000);
        }
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 1);
        paired_main_at(q, PAIRED_MAIN, 129, 136, 112);
        qtest_writel(q, PAIRED + 0x24, UINT32_MAX);
        work_ack(q);
        pipeline_step(q, 2000000);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 1);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "rejected"), ==, 0);
        qtest_quit(q);
        unlink(scene);
        unlink(bundle);
    }

    for (unsigned kick = 0; kick < 2; kick++) {
        g_autofree char *bundle = NULL;
        g_autofree char *scene = NULL;
        QTestState *q = paired_start(&bundle, &scene, true, false);

        paired_saturation(q, PAIRED, 1, 256, 256, 17);
        qtest_writel(q, PAIRED + 0x10, 1);
        qtest_writel(q, PAIRED + 8, 1);
        /* Alias publication uploads the new first slot before the second. */
        paired_saturation(q, PAIRED, 0, 128, 128, 17);
        paired_raw_input(q);
        pipeline_step(q, 1000000);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 0);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "rejected"), ==, 0);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "blocked"), >, 0);
        paired_untouched(q);
        if (kick) {
            /* An explicit admission with a final different pair is unsupported. */
            qtest_writel(q, PAIRED + 0x10, 1);
            g_assert_cmpuint(property_at(q, PAIRED_PATH, "rejected"), ==, 1);
            paired_untouched(q);
        } else {
            paired_saturation(q, PAIRED, 1, 128, 128, 17);
            pipeline_step(q, 2000000);
            g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 1);
            g_assert_cmpuint(property_at(q, PAIRED_PATH, "rejected"), ==, 0);
            paired_main_at(q, PAIRED_MAIN, 129, 136, 112);
            qtest_writel(q, PAIRED + 0x24, UINT32_MAX);
            work_ack(q);
            pipeline_step(q, 2000000);
            g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 1);
        }
        qtest_quit(q);
        unlink(scene);
        unlink(bundle);
    }
}

static void test_paired_saturation_history(void)
{
    static const uint8_t gray[] = {
        'P', '5', '\n', '1', ' ', '1', '\n', '2', '5', '5', '\n', 129,
    };
    g_autofree char *bundle = NULL;
    g_autofree char *scene = NULL;
    QTestState *q = paired_start(&bundle, &scene, true, true);

    for (unsigned phase = 0; phase < 2; phase++) {
        paired_raw_input(q);
        pipeline_step(q, 1000000);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, phase + 1);
        qtest_writel(q, PAIRED + 0x24, UINT32_MAX);
        work_ack(q);
    }
    paired_saturation(q, PAIRED, 0, 384, 384, 17);
    g_assert_true(g_file_set_contents(scene, (const char *)gray,
                                      sizeof(gray), NULL));
    qtest_qmp_assert_success(q,
        "{'execute':'qom-set','arguments':{'path':'/machine/cmos',"
        "'property':'image','value':%s}}", scene);
    paired_raw_input(q);
    pipeline_step(q, 1000000);
    g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 3);
    g_assert_cmpuint(qtest_readw(q, PAIRED_WORK), ==, 2068);
    g_assert_cmpuint(qtest_readw(q, PAIRED_WORK + 18 * 80), ==, 16);
    qtest_quit(q);
    unlink(scene);
    unlink(bundle);
}

static void test_paired_colorfilter(void)
{
    static const char header[] = "P5\n64 36\n255\n";
    const unsigned prefix = sizeof(header) - 1;

    for (unsigned sign = 0; sign < 2; sign++) {
        g_autofree char *bundle = NULL;
        g_autofree char *scene = NULL;
        g_autofree char *image = g_malloc(prefix + 64 * 36);
        uint8_t pixels[64 * 28];
        QTestState *q = paired_start(&bundle, &scene, true, false);

        memcpy(image, header, prefix);
        memset(image + prefix, sign ? 64 : 128, 64 * 36);
        image[prefix + 17 * 64 + 31] = (char)(sign ? 58 : 134);
        g_assert_true(g_file_set_contents(scene, image, prefix + 64 * 36, NULL));
        qtest_qmp_assert_success(q,
            "{'execute':'qom-set','arguments':{'path':'/machine/cmos',"
            "'property':'image','value':%s}}", scene);
        paired_colorfilter(q, PAIRED, 31, 51, false);
        qtest_writel(q, PAIRED + 0x10, 1);
        qtest_writel(q, PAIRED + 8, 1);
        paired_raw_input(q);
        pipeline_step(q, 1000000);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 1);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "rejected"), ==, 0);
        qtest_memread(q, PAIRED_MAIN, pixels, sizeof(pixels));
        /* RAW2056+96/1028-97 -> spatial2104/980 -> tail2072/1016.
         * Actual MAIN crop(4,4), independent WORK mean2060/1025.
         * Tail restores axial neighbors to2056/1028. DD's rational gate
         * therefore adds round(-49152/40960)=-1 or round(39168/40960)=1:
         * actual MAIN2071/1017 becomes Y129/64 after the complete chain.
         */
        g_assert_cmpuint(pixels[13 * 64 + 27], ==, sign ? 64 : 129);
        g_assert_cmpuint(qtest_readw(q, PAIRED_WORK + 8 * 80 + 15 * 2), ==,
                         sign ? 1025 : 2060);
        paired_pixels(q, PAIRED_GRID, 0);
        for (unsigned row = 0; row < 28; row++) {
            for (unsigned x = 56; x < 64; x++) {
                g_assert_cmphex(pixels[row * 64 + x], ==, 0xa5);
            }
        }
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

static void test_paired_colorfilter_boundaries(void)
{
    static const struct { unsigned offset; uint32_t value; } invalid[] = {
        { 0x900, 0x01910000 }, { 0x900, 0x0000012d },
        { 0x904, 0x00650000 }, { 0x908, 0x00790000 },
        { 0x90c, 0x02040202 }, { 0x90c, 0x01020202 },
        { 0x910, 1 }, { 0x910, 0x100 }, { 0x910, 2 },
        { 0x914, 0x00510000 }, { 0x918, 0x00010000 },
        { 0x91c, 0x02030202 }, { 0x920, 2 },
        { 0x924, 0x01000000 }, { 0x924, 0x00200000 }, { 0x924, 1 },
        { 0x928, 1 }, { 0x94c, 0x800 }, { 0x950, 0x00010000 },
        { 0x954, 0x0bb70190 }, { 0x958, 0x10808031 },
        { 0x95c, 0x01e10000 }, { 0x960, 0x01e10000 },
        { 0x964, 0x021d0000 }, { 0x968, 0x02020202 },
        { 0x96c, 2 }, { 0x970, 0x01f50000 },
        { 0x974, 0x00010000 }, { 0x978, 0x02030202 },
        { 0x97c, 2 }, { 0x980, 0x00000100 }, { 0x984, 1 },
        { 0x9a8, 1 }, { 0x9ac, 0x00040009 }, { 0x9ac, 0x00050097 },
        { 0x9b0, 1 }, { 0x9b4, 1 }, { 0x9b4, 0x202 },
        { 0x9b8, 0x8300 }, { 0x9b8, 32 }, { 0x9b8, 0x01000000 },
        { 0x9bc, 0x010000 }, { 0x9bc, 0x0200c9 },
        { 0x9c0, 1 }, { 0x9c4, 0x0704 }, { 0x9c4, 0x0803 },
        { 0x9c4, 0x01000804 }, { 0x9d8, 1 },
    };

    for (unsigned test = 0; test < ARRAY_SIZE(invalid); test++) {
        g_autofree char *bundle = NULL;
        g_autofree char *scene = NULL;
        QTestState *q = paired_start(&bundle, &scene, true, false);

        qtest_writel(q, PAIRED + invalid[test].offset, invalid[test].value);
        qtest_writel(q, PAIRED + 0x10, 1);
        qtest_writel(q, PAIRED + 8, 1);
        paired_raw_input(q);
        pipeline_step(q, 1000000);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 0);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "rejected"), >, 0);
        paired_untouched(q);
        qtest_quit(q);
        unlink(scene);
        unlink(bundle);
    }
}

static void test_paired_colorfilter_publication(void)
{
    g_autofree char *bundle = NULL;
    g_autofree char *scene = NULL;
    QTestState *q = paired_start(&bundle, &scene, true, false);
    uint32_t words[51];

    for (unsigned i = 0; i < 51; i++) {
        words[i] = qtest_readl(q, PAIRED + (i == 50 ? 0x9d8 : 0x900 + 4 * i));
    }
    /* Every member, including zero coefficients/fixed qualifiers, is required. */
    for (unsigned missing = 0; missing <= 51; missing++) {
        qtest_system_reset(q);
        paired_configure(q, true, PAIRED, 7, 8, 8, 6, 0, 64, 17);
        for (unsigned i = 0; i < 51; i++) {
            unsigned offset = i == 50 ? 0x9d8 : 0x900 + 4 * i;

            g_assert_cmphex(qtest_readl(q, PAIRED + offset), ==, 0);
            if (i != missing && missing != 51) {
                qtest_writel(q, PAIRED + offset, words[i]);
            }
        }
        qtest_writel(q, PAIRED + 0x10, 1);
        qtest_writel(q, PAIRED + 8, 1);
        paired_raw_input(q);
        pipeline_step(q, 1000000);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 0);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "rejected"), >, 0);
        paired_untouched(q);
    }
    qtest_quit(q);
    unlink(scene);
    unlink(bundle);
}

static void test_paired_colorfilter_lifecycle(void)
{
    for (unsigned mode = 0; mode < 3; mode++) {
        g_autofree char *bundle = NULL;
        g_autofree char *scene = NULL;
        QTestState *q = paired_start(&bundle, &scene, true, false);

        paired_colorfilter(q, PAIRED, 31, 51, false);
        qtest_writel(q, PAIRED + 0x10, 1);
        qtest_writel(q, PAIRED + 8, 1);
        work_input(q);
        work_rise(q);
        pipeline_step(q, 1000000);
        if (mode == 2) {
            qtest_system_reset(q);
            for (unsigned offset = 0x900; offset <= 0x9c4; offset += 4) {
                g_assert_cmphex(qtest_readl(q, PAIRED + offset), ==, 0);
            }
            g_assert_cmphex(qtest_readl(q, PAIRED + 0x9d8), ==, 0);
        } else {
            paired_colorfilter(q, PAIRED, mode ? 30 : 31, 51, false);
        }
        pipeline_step(q, 2000000);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, mode != 2);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "cancelled"), ==, mode == 2);
        if (mode != 2) {
            paired_pixels(q, PAIRED_MAIN, 0);
            paired_pixels(q, PAIRED_WORK, 0);
            paired_pixels(q, PAIRED_GRID, 0);
        } else {
            paired_untouched(q);
            g_assert_cmphex(qtest_readl(q, PAIRED + 0x24), ==, 0);
        }
        if (mode == 1) {
            qtest_writel(q, PAIRED + 0x24, UINT32_MAX);
            work_ack(q);
            paired_raw_input(q);
            pipeline_step(q, 1000000);
            g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 2);
            g_assert_cmpuint(property_at(q, PAIRED_PATH, "rejected"), ==, 0);
            paired_pixels(q, PAIRED_MAIN, 1);
            paired_pixels(q, PAIRED_WORK, 1);
            paired_pixels(q, PAIRED_GRID, 1);
        }
        qtest_quit(q);
        unlink(scene);
        unlink(bundle);
    }
}

static void test_paired_colorfilter_history(void)
{
    static const uint8_t gray[] = {
        'P', '5', '\n', '1', ' ', '1', '\n', '2', '5', '5', '\n', 129,
    };

    for (unsigned changed = 0; changed < 2; changed++) {
        g_autofree char *bundle = NULL;
        g_autofree char *scene = NULL;
        QTestState *q = paired_start(&bundle, &scene, true, true);

        for (unsigned phase = 0; phase < 2; phase++) {
            paired_raw_input(q);
            pipeline_step(q, 1000000);
            g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==,
                             phase + 1);
            qtest_writel(q, PAIRED + 0x24, UINT32_MAX);
            work_ack(q);
        }
        /* Uniform pixels make either strength neutral, but a changed packet
         * invalidates temporal history; an equal write must preserve it.
         */
        paired_online_republish(q, 0x9b8, changed);
        g_assert_true(g_file_set_contents(scene, (const char *)gray,
                                          sizeof(gray), NULL));
        qtest_qmp_assert_success(q,
            "{'execute':'qom-set','arguments':{'path':'/machine/cmos',"
            "'property':'image','value':%s}}", scene);
        paired_raw_input(q);
        pipeline_step(q, 1000000);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 3);
        g_assert_cmpuint(qtest_readw(q, PAIRED_WORK), ==, changed ? 2072 : 2068);
        g_assert_cmpuint(qtest_readw(q, PAIRED_WORK + 18 * 80), ==,
                         changed ? 0 : 16);
        qtest_quit(q);
        unlink(scene);
        unlink(bundle);
    }
}

static void paired_radial_tables(QTestState *q, unsigned words)
{
    for (unsigned i = 0; i < words; i++) {
        qtest_writel(q, PAIRED + 0x1200 + 4 * i, 0);
    }
}

static void test_paired_radial(void)
{
    static const int identity[] = { 1024, 0, 0, 0, 1024, 0, 0, 0, 1024 };

    for (unsigned cfa = 0; cfa < 2; cfa++) {
        for (unsigned blue = 0; blue < 2; blue++) {
            for (unsigned operation = 0; operation < 3; operation++) {
                bool negative = operation == 1, origin = operation == 2;
                g_autofree char *bundle = NULL;
                g_autofree char *scene = NULL;
                unsigned rows = cfa ? 72 : 36, channels = cfa ? 3 : 1;
                g_autofree char *header = g_strdup_printf(
                    "P%c\n64 %u\n255\n", cfa ? '6' : '5', rows);
                size_t prefix = strlen(header);
                size_t length = prefix + rows * 64 * channels;
                g_autofree uint8_t *image = g_malloc0(length);
                int b[9] = { 0 };
                uint8_t pixels[64 * 28];
                QTestState *q;

                if (origin && (cfa || blue)) {
                    continue;
                }
                q = paired_start(&bundle, &scene, true, false);

                memcpy(image, header, prefix);
                for (unsigned y = 0; y < rows; y++) {
                    for (unsigned x = 32; x < 64; x++) {
                        image[prefix + channels * (y * 64 + x) +
                              (cfa && blue ? 2 : 0)] = 255;
                    }
                }
                g_assert_true(g_file_set_contents(scene, (const char *)image,
                                                  length, NULL));
                qtest_qmp_assert_success(q,
                    "{'execute':'qom-set','arguments':{'path':'/machine/cmos',"
                    "'property':'image','value':%s}}", scene);
                b[blue ? 2 : 0] = 512;
                paired_matrix(q, PAIRED, 0xb00, identity, 13, false);
                paired_matrix(q, PAIRED, 0xb64, b, 12, false);
                paired_radial(q, PAIRED, origin ? 0 : 65535, 18,
                    blue ? 0 : negative ? -128 : 127,
                    blue ? negative ? -128 : 127 : 0, 6);
                /* Authored zero curves retain both nonzero head coefficients. */
                paired_radial_tables(q, 18);
                if (origin) {
                    qtest_writel(q, PAIRED + 0x700, 0x101);
                    qtest_writel(q, PAIRED + 0x708, 0x02000200);
                    qtest_writel(q, PAIRED + 0x70c, 0x02000200);
                    qtest_writel(q, PAIRED + 0x718, 9 << 16);
                    qtest_writel(q, PAIRED + 0x720, 4095);
                }
                /* Odd RAW ROI plus odd MAIN crop; wire center remains in the
                 * source coordinate system and is adjusted only at snapshot.
                 */
                qtest_writel(q, PAIRED + 0x44, (1 << 16) | 1);
                qtest_writel(q, PAIRED + 0x48, (60 << 16) | 34);
                qtest_writel(q, PAIRED + 0x9c, 33);
                qtest_writel(q, PAIRED + 0xc4, 60);
                qtest_writel(q, PAIRED + 0x204, (34 << 16) | 60);
                /* RAW ROI is34 rows: WORK halves are17 rows, not the
                 * original36-row source fixture's18-row allocation. */
                for (unsigned phase = 0; phase < 4; phase++) {
                    uint32_t work = PAIRED_WORK + (phase % 2) * 0x2000;

                    qtest_writel(q, PAIRED + 0x440 + 4 * phase, work + 80 * 17);
                    qtest_writel(q, PAIRED + 0x460 + 4 * phase, work + 80 * 17);
                }
                qtest_writel(q, PAIRED + 0x400, (3 << 16) | 28);
                qtest_writel(q, PAIRED + 0x40c, (54 << 16) | 3);
                qtest_writel(q, PAIRED + 0x10, 1);
                qtest_writel(q, PAIRED + 8, 1);
                if (cfa) {
                    paired_cfa_input(q);
                } else {
                    paired_raw_input(q);
                    pipeline_step(q, 1000000);
                }
                g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"),
                                 ==, 1);
                qtest_memread(q, PAIRED_MAIN, pixels, sizeof(pixels));
                if (origin) {
                    /* Wire center0 - RAW origin1 = -1. The measured step's
                     * RAW3583 becomes3615, then pedestal9 gives3624 -> Y227.
                     * Omitting the signed ROI adjustment instead produces226.
                     */
                    g_assert_cmpuint(pixels[28], ==, 227);
                    g_assert_cmpuint(pixels[0], ==, 1);
                    g_assert_cmpuint(pixels[53], ==, 255);
                }
                for (unsigned y = 0; y < 28; y++) {
                    for (unsigned x = 0; x < 64; x++) {
                        /* Positive scales the left edge beyond the source;
                         * negative scales to its right edge. The unchanged
                         * step would contain both dark and bright pixels.
                         */
                        if (!origin || x >= 54) {
                            g_assert_cmpuint(pixels[64 * y + x], ==,
                                x < 54 ? negative ? 255 : 0 : 0xa5);
                        }
                    }
                }
                g_assert_cmphex(qtest_readl(q, PAIRED_MAIN - 4), ==,
                                0xa5a5a5a5);
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
    }
}

static void test_paired_radial_boundaries(void)
{
    static const struct { unsigned offset; uint32_t value; } invalid[] = {
        { 0x724, 269 }, { 0x72c, 0x30204 }, { 0x72c, 0x01030205 },
        { 0x730, 0x04000000 }, { 0x730, 0x00000400 },
        { 0x730, 0x00800000 }, { 0x730, 0x00000080 },
        { 0x730, 0x037f0000 }, { 0x730, 0x0000037f },
        { 0x734, 0x0420 }, { 0x734, 0x0920 },
        { 0x734, 0x051f }, { 0x734, 0x053f }, { 0x734, 0x00010520 },
        { 0x738, 0 }, { 0x738, 0x0404 }, { 0x738, 0x0102 },
        { 0x738, 0x00010101 },
        /* Partial written zeros cannot impersonate a complete publication. */
        { 0x1200, 0 }, { 0x1220, 0 }, { 0x1224, 0 }, { 0x1244, 0 },
        { 0x1200, 1 },
    };

    for (unsigned mode = 0; mode < ARRAY_SIZE(invalid) + 6; mode++) {
        g_autofree char *bundle = NULL;
        g_autofree char *scene = NULL;
        QTestState *q = paired_start(&bundle, &scene, true, false);

        if (mode < ARRAY_SIZE(invalid)) {
            qtest_writel(q, PAIRED + invalid[mode].offset, invalid[mode].value);
        } else {
            qtest_system_reset(q);
            paired_configure(q, true, PAIRED, 7, 8, 8,
                             mode - ARRAY_SIZE(invalid), 51, 64, 17);
        }
        qtest_writel(q, PAIRED + 0x10, 1);
        qtest_writel(q, PAIRED + 8, 1);
        paired_raw_input(q);
        pipeline_step(q, 1000000);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 0);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "rejected"), >, 0);
        paired_untouched(q);
        qtest_quit(q);
        unlink(scene);
        unlink(bundle);
    }
}

static void test_paired_radial_lifecycle(void)
{
    for (unsigned mode = 0; mode < 4; mode++) {
        g_autofree char *bundle = NULL;
        g_autofree char *scene = NULL;
        QTestState *q = paired_start(&bundle, &scene, true, false);

        paired_radial(q, PAIRED, 32, 18, -128, 127, 6);
        qtest_writel(q, PAIRED + 0x10, 1);
        qtest_writel(q, PAIRED + 8, 1);
        work_input(q);
        work_rise(q);
        pipeline_step(q, 1000000);
        if (mode == 3) {
            qtest_system_reset(q);
            for (unsigned offset = 0x724; offset <= 0x738; offset += 4) {
                g_assert_cmphex(qtest_readl(q, PAIRED + offset), ==, 0);
            }
        } else if (mode == 2) {
            qtest_writel(q, PAIRED + 0x1200, 0);
        } else {
            paired_radial(q, PAIRED, 32, 18, mode ? -127 : -128, 127, 6);
        }
        pipeline_step(q, 2000000);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==,
                         mode != 3);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "cancelled"), ==,
                         mode == 3);
        if (mode != 3) {
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

static void test_paired_radial_tables(void)
{
    for (unsigned mode = 0; mode < 54; mode++) {
        g_autofree char *bundle = NULL;
        g_autofree char *scene = NULL;
        QTestState *q = paired_start(&bundle, &scene, true, false);

        if (mode < 17) {
            /* Every cold prefix1..17 is incomplete, including one full slot. */
            paired_radial_tables(q, mode + 1);
        } else if (mode < 35) {
            unsigned missing = mode - 17;

            paired_radial_tables(q, 18);
            for (unsigned word = 0; word < 18; word++) {
                if (word != missing) {
                    qtest_writel(q, PAIRED + 0x1200 + 4 * word, 0);
                }
            }
        } else {
            unsigned changed = mode - 35;

            for (unsigned word = 0; word < 18; word++) {
                /* All18 words, either signed coefficient, and both pad halves. */
                uint32_t value = word == changed ?
                    (word % 9 == 8 ? 0x10000 : 1) : 0;

                if (changed == 18 && !word) {
                    value = 0xffff0000;
                }
                qtest_writel(q, PAIRED + 0x1200 + 4 * word, value);
            }
        }
        qtest_writel(q, PAIRED + 0x10, 1);
        qtest_writel(q, PAIRED + 8, 1);
        paired_raw_input(q);
        pipeline_step(q, 1000000);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 0);
        if (mode == 25 || mode == 34) {
            /* A missing final word leaves a valid warm staging prefix. */
            g_assert_cmpuint(property_at(q, PAIRED_PATH, "rejected"), ==, 0);
            g_assert_cmpuint(property_at(q, PAIRED_PATH, "blocked"), >, 0);
        } else {
            g_assert_cmpuint(property_at(q, PAIRED_PATH, "rejected"), >, 0);
        }
        paired_untouched(q);
        qtest_quit(q);
        unlink(scene);
        unlink(bundle);
    }
}

static void test_paired_radial_tables_lifecycle(void)
{
    for (unsigned mode = 0; mode < 5; mode++) {
        g_autofree char *bundle = NULL;
        g_autofree char *scene = NULL;
        QTestState *q = paired_start(&bundle, &scene, true, false);

        paired_radial_tables(q, 18);
        qtest_writel(q, PAIRED + 0x10, 1);
        qtest_writel(q, PAIRED + 8, 1);
        if (mode < 3) {
            work_input(q);
            work_rise(q);
            pipeline_step(q, 1000000);
            if (mode == 0) {
                paired_radial_tables(q, 18);
            } else if (mode == 1) {
                paired_radial_tables(q, 10);
            } else {
                qtest_writel(q, PAIRED + 0x1200, 1);
            }
            pipeline_step(q, 2000000);
            g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 1);
            g_assert_cmpuint(property_at(q, PAIRED_PATH, "cancelled"), ==, 0);
            paired_pixels(q, PAIRED_MAIN, 0);
        } else if (mode == 3) {
            paired_radial_tables(q, 10);
            paired_raw_input(q);
            pipeline_step(q, 1000000);
            g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 0);
            g_assert_cmpuint(property_at(q, PAIRED_PATH, "blocked"), >, 0);
            g_assert_cmpuint(property_at(q, PAIRED_PATH, "rejected"), ==, 0);
            for (unsigned word = 10; word < 18; word++) {
                qtest_writel(q, PAIRED + 0x1200 + 4 * word, 0);
            }
            pipeline_step(q, 2000000);
            g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 1);
            paired_pixels(q, PAIRED_MAIN, 0);
        } else {
            work_input(q);
            work_rise(q);
            pipeline_step(q, 1000000);
            qtest_system_reset(q);
            for (unsigned offset = 0x1200; offset <= 0x1244; offset += 4) {
                g_assert_cmphex(qtest_readl(q, PAIRED + offset), ==, 0);
            }
            pipeline_step(q, 2000000);
            g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 0);
            g_assert_cmpuint(property_at(q, PAIRED_PATH, "cancelled"), ==, 1);
            paired_untouched(q);
            paired_configure(q, true, PAIRED, 7, 8, 8, 6, 51, 64, 17);
            paired_radial_tables(q, 9);
            qtest_writel(q, PAIRED + 0x10, 1);
            qtest_writel(q, PAIRED + 8, 1);
            paired_raw_input(q);
            pipeline_step(q, 1000000);
            g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 0);
            g_assert_cmpuint(property_at(q, PAIRED_PATH, "rejected"), >, 0);
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
                                  mode == 1 ? 0x470 : 0x30c), 0);
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
        paired_raw_input(q);
        pipeline_step(q, 1000000);
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

#define PAIRED_GATE_OUTPUT (RAM + 0x200000)
#define PAIRED_GATE_MAP (RAM + 0x180000)
#define PAIRED_GATE_AFFINE (RAM + 0x181000)

static unsigned paired_gate_block(uint8_t *data, const uint32_t *words,
                                  unsigned offset, unsigned count, bool last)
{
    stl_le_p(data, offset | (count << 16) |
                   (last ? 0x01000000 : 0x11000000));
    for (unsigned i = 1; i < count; i++) {
        stl_le_p(data + 4 * i, words[offset / 4 + i - 1]);
    }
    return 4 * count;
}

static void paired_gate_command(QTestState *q, unsigned index,
                                const uint32_t *words)
{
    static const unsigned ports[] = {
        0xe60, 0x900, 0xde0, 0x8a0, 0x8c0, 0x8e0,
        0x9e0, 0x940, 0xba0, 0x920,
    };
    uint8_t data[512];
    unsigned n = 0;
    uint32_t address = RAM + 0x182000 + 512 * index;
    unsigned port = ports[index];

    if (!index) {
        n += paired_gate_block(data + n, words, 0, 4, false);
        n += paired_gate_block(data + n, words, 0x200, 81, false);
        n += paired_gate_block(data + n, words, 0x400, 4, true);
    } else if (index == 1) {
        n += paired_gate_block(data, words, 0, 34, true);
    } else if (index >= 3 && index <= 5) {
        n += paired_gate_block(data + n, words, 0, 28, false);
        n += paired_gate_block(data + n, words, 0x78, 4, true);
    } else if (index == 9) {
        n += paired_gate_block(data + n, words, 0, 3, false);
        n += paired_gate_block(data + n, words, 0x10, 7, false);
        n += paired_gate_block(data + n, words, 0x40, 2, false);
        n += paired_gate_block(data + n, words, 0x60, 9, true);
    } else {
        n += paired_gate_block(data + n, words, 0, 10, false);
        n += paired_gate_block(data + n, words, 0x40, 8, false);
        n += paired_gate_block(data + n, words, 0x80, 17, true);
    }
    qtest_memwrite(q, address, data, n);
    qtest_writel(q, PAIRED_GATE + port + 0x14, 1);
    qtest_writel(q, PAIRED_GATE + port + 0x14, 0);
    if (index >= 6 && index <= 8) {
        qtest_writel(q, PAIRED_GATE + port + 8, 0x200087e0);
        qtest_writel(q, PAIRED_GATE + port + 0xc, 0x88000);
    }
    qtest_writel(q, PAIRED_GATE + port + 0x10, address);
}

static void paired_gate_window(uint32_t *words, bool terminal)
{
    words[0x18 / 4] = terminal ? 0xff010000 : 0xff000000;
    words[0x80 / 4] = 6;
    words[0x8c / 4] = words[0x94 / 4] = 0xff;
    words[0xa8 / 4] = words[0xb0 / 4] = 0xff;
}

/* Synthetic complete MAIN graph: real imported packets, identity Q8 warp and
 * three actual affine outputs. The 64x32 intermediate replicates the borders
 * of the paired56x28 input; constant source pixels remain exact. RUN is left
 * stopped so the finite input slot can fill before the next private job.
 */
static void paired_gate_main(QTestState *q)
{
    uint32_t words[0x40c / 4] = { 0 };
    static const unsigned routes[] = { 0x40, 7, 0x46 };
    static const struct { unsigned offset; uint32_t value; } config[] = {
        { 0x2100, 0x121 }, { 0x2104, 0xc2 }, { 0x2110, 0x200087e0 },
        { 0x2114, 0x88000 }, { 0x2118, 0x139 }, { 0x2600, 0x305 },
        { 0x2604, 28 }, { 0x2608, 1 }, { 0x2618, 3 },
        { 0x4050, 2 }, { 0x4054, 64 | (8 << 16) },
    };

    paired_gate_slot(q, 56);
    words[0] = 8;
    words[1] = 0x30;
    words[2] = 0x202;
    words[0x400 / 4] = 0x31;
    words[0x404 / 4] = 8;
    words[0x408 / 4] = 0xfe;
    paired_gate_command(q, 0, words);
    memset(words, 0, sizeof(words));
    words[0] = 0x6a;
    words[1] = words[2] = words[3] = 0xff;
    words[0x10 / 4] = 0x30;
    words[0x14 / 4] = 0x10000000;
    words[0x18 / 4] = 0x4080;
    words[0x1c / 4] = 0x120;
    words[0x28 / 4] = 1;
    for (unsigned offset = 0x30; offset <= 0x3c; offset += 4) {
        words[offset / 4] = 0x100000;
    }
    for (unsigned offset = 0x40; offset <= 0x4c; offset += 4) {
        words[offset / 4] = 0xff;
    }
    words[0x50 / 4] = (4 << 16) | 1;
    words[0x54 / 4] = PAIRED_GATE_MAP;
    words[0x64 / 4] = 4;
    words[0x68 / 4] = 0x40302;
    words[0x78 / 4] = 0x31;
    words[0x7c / 4] = 0x3000;
    words[0x80 / 4] = 0x10;
    paired_gate_command(q, 1, words);
    for (unsigned row = 0; row < 4; row++) {
        uint8_t record[40] = { 0 };

        for (unsigned corner = 0; corner < 4; corner++) {
            stl_le_p(record + 8 * corner, (corner & 1 ? 64 : 0) << 8);
            stl_le_p(record + 8 * corner + 4,
                      (row * 8 + (corner & 2 ? 8 : 0)) << 8);
        }
        stl_le_p(record + 32, 0x306);
        qtest_memwrite(q, PAIRED_GATE_MAP + row * 40, record, sizeof(record));
    }
    memset(words, 0, sizeof(words));
    paired_gate_window(words, false);
    words[0] = 0x90000;
    words[1] = 0xff0000;
    words[2] = 0x10500;
    words[0x40 / 4] = 63 << 16;
    words[0x44 / 4] = 31 << 16;
    words[0x48 / 4] = 0x400040;
    words[0x4c / 4] = 0x80008;
    paired_gate_command(q, 2, words);
    for (unsigned branch = 0; branch < 3; branch++) {
        unsigned width = branch ? 28 : 56, height = branch ? 14 : 28;
        unsigned scale = branch ? 2 : 1, slot = branch + 3;
        uint8_t transform[32] = { 0 };
        uint32_t parameters[] = {
            0x9000201, 0x15500, 0, 0, 64, 64, 0, 0,
            width | (height << 16), 0, 0x800, 0, 0,
        };

        memset(words, 0, sizeof(words));
        words[0] = branch == 1 ? 0xa : 0xff;
        words[1] = branch == 0 ? 0x14 : branch == 2 ? 0x16 : 0xff;
        words[2] = words[3] = 0xff;
        words[0x10 / 4] = branch == 2 ? 0x10 : 0x13;
        words[0x14 / 4] = 0x10000000;
        words[0x18 / 4] = 0x5050;
        words[0x1c / 4] = 0x300;
        words[0x20 / 4] = branch == 0 ? 0x10 : 0;
        words[0x40 / 4] = words[0x44 / 4] = words[0x48 / 4] = 0xff;
        words[0x50 / 4] = 0x10001;
        words[0x54 / 4] = PAIRED_GATE_AFFINE + branch * 32;
        words[0x64 / 4] = 0xd;
        words[0x78 / 4] = 1;
        paired_gate_command(q, 3 + branch, words);
        stl_le_p(transform, branch == 0 ? 0x80000000 : 0);
        stl_le_p(transform + 4, branch == 0 ? 0x80000000 : 0);
        stl_le_p(transform + 8, scale << 16);
        stl_le_p(transform + 20, scale << 16);
        stl_le_p(transform + 24, width | (height << 16));
        qtest_memwrite(q, PAIRED_GATE_AFFINE + branch * 32,
                       transform, sizeof(transform));
        memset(words, 0, sizeof(words));
        paired_gate_window(words, true);
        words[0] = routes[branch] << 16;
        words[2] = 0x10000 | slot;
        words[0x40 / 4] = (width - 1) << 16;
        words[0x44 / 4] = (height - 1) << 16;
        words[0x48 / 4] = branch == 2 ? 0x400040 : 0x1000100;
        words[0x4c / 4] = 0x20002;
        paired_gate_command(q, 6 + branch, words);
        for (unsigned word = 0; word < ARRAY_SIZE(parameters); word++) {
            qtest_writel(q, PAIRED_GATE + 0x400 + slot * 64 + word * 4,
                         parameters[word]);
        }
        qtest_writel(q, PAIRED_GATE + 0x2c, slot);
        for (unsigned phase = 0; phase < 4; phase++) {
            uint32_t address = PAIRED_GATE_OUTPUT + phase * 0x10000 + branch * 0x4000;

            qtest_memset(q, address - 16, 0xa5, 0xc20);
            qtest_writel(q, PAIRED_GATE + 0x2200 + phase * 64 + slot * 4, address);
        }
    }
    memset(words, 0, sizeof(words));
    words[0x10 / 4] = 0x20200;
    words[0x14 / 4] = 0x20201;
    words[0x18 / 4] = 0x20202;
    words[0x1c / 4] = 0x203;
    paired_gate_command(q, 9, words);
    for (unsigned i = 0; i < ARRAY_SIZE(config); i++) {
        qtest_writel(q, PAIRED_GATE + config[i].offset, config[i].value);
    }
    for (unsigned phase = 0; phase < 4; phase++) {
        qtest_writel(q, PAIRED_GATE + 0x2200 + phase * 64 + 8 * 4, 0x40000000);
    }
    qtest_writel(q, PAIRED_GATE + 0x10, 0x61);
    qtest_writel(q, PAIRED_GATE + 0x14, 8);
}

static void paired_gate_output(QTestState *q, unsigned phase, bool present,
                                uint8_t luma, uint8_t u, uint8_t v)
{
    for (unsigned branch = 0; branch < 3; branch++) {
        unsigned width = branch ? 28 : 56, height = branch ? 14 : 28;
        uint32_t address = PAIRED_GATE_OUTPUT + phase * 0x10000 + branch * 0x4000;
        uint8_t expected[0xc20], actual[sizeof(expected)];

        memset(expected, 0xa5, sizeof(expected));
        if (present) {
            for (unsigned row = 0; row < height; row++) {
                memset(expected + 16 + row * 64, luma, width);
            }
            for (unsigned row = 0; row < height / 2; row++) {
                for (unsigned x = 0; x < width; x++) {
                    expected[16 + 0x800 + row * 64 + x] = x & 1 ? v : u;
                }
            }
        }
        qtest_memread(q, address - 16, actual, sizeof(actual));
        g_assert_cmpmem(actual, sizeof(actual), expected, sizeof(expected));
    }
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

static void paired_stream_samples(QTestState *q, const char *path, bool present,
                                   uint8_t luma, uint8_t u, uint8_t v)
{
    QDict *reply = qtest_qmp(q, "{'execute':'qom-get','arguments':"
                               "{'path':%s,'property':'input-data'}}", path);
    gsize length;
    g_autofree guchar *pixels = g_base64_decode(qdict_get_str(reply, "return"),
                                               &length);

    g_assert_cmpuint(length, ==, present ? 56 * 42 : 0);
    if (present) {
        for (unsigned i = 0; i < 56 * 42; i++) {
            g_assert_cmpuint(pixels[i], ==, i < 56 * 28 ? luma :
                             i % 2 ? v : u);
        }
    }
    qobject_unref(reply);
}

static void paired_stream_pixels(QTestState *q, const char *path, bool present)
{
    paired_stream_samples(q, path, present, 129, 144, 96);
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
    paired_raw_input(q);
    pipeline_step(q, 1000000);
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
    paired_raw_input(q);
    pipeline_step(q, 1000000);
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
        pipeline_step(q, 1000000);
        if (mode == 0) {
            paired_gate_slot(q, 54);
        } else if (mode == 1) {
            paired_gate_input(q, 9);
        } else if (mode == 2) {
            qtest_system_reset(q);
        }
        pipeline_step(q, 2000000);
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

    paired_configure(q, true, peer, 7, 8, 8, 6, 51, 64, 17);
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
    pipeline_step(q, 1000000);
    g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 0);
    g_assert_cmpuint(property_at(q, PAIRED_PATH, "blocked"), >, 0);
    pipeline_step(q, 1000000);
    g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 1);
    paired_stream_pixels(q, gate, true);
    qtest_writel(q, PAIRED + 0x24, UINT32_MAX);
    pipeline_step(q, 2000000);
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
        paired_raw_input(q);
        pipeline_step(q, 1000000);
        paired_stream_pixels(q, gate, true);
        if (mode == 0) {
            paired_gate_input(q, 9);
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
        pipeline_step(q, 2000000);
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
        paired_raw_input(q);
        pipeline_step(q, 1000000);
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

static void test_paired_cycle(void)
{
    g_autofree char *bundle = NULL;
    g_autofree char *scene = NULL;
    QTestState *q = paired_start(&bundle, &scene, true, true);

    for (unsigned frame = 0; frame < 12; frame++) {
        paired_raw_input(q);
        pipeline_step(q, 1000000);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==,
                         frame + 1);
        paired_pixels(q, PAIRED_MAIN, frame % 2);
        paired_pixels(q, PAIRED_WORK, frame % 2);
        paired_pixels(q, PAIRED_GRID, frame % 4);
        g_assert_cmphex(qtest_readl(q, PAIRED + 0x24), ==,
                        0x1002 | ((frame % 4 == 3) ? 0x400 : 0));
        qtest_writel(q, PAIRED + 0x24, UINT32_MAX);
        work_ack(q);
        /* Neither ACK nor elapsed time creates a new RAW generation. */
        pipeline_step(q, 2000000);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==,
                         frame + 1);
    }
    qtest_quit(q);
    unlink(scene);
    unlink(bundle);
}

static void test_paired_raw_reuse(void)
{
    static const uint8_t gray[] = {
        'P', '5', '\n', '1', ' ', '1', '\n', '2', '5', '5', '\n', 130,
    };
    g_autofree char *bundle = NULL;
    g_autofree char *scene = NULL;
    QTestState *q = paired_start(&bundle, &scene, true, false);
    uint8_t expected[0xce0], actual[sizeof(expected)];

    /* A complete tuple may reuse one physical RAW span at different times.
     * Even its future duplicate must not block the current fresh phase.
     */
    qtest_writel(q, PAIRED + 0x224, WORK_BUFFER + 0x1000);
    qtest_writel(q, PAIRED + 0x31c, WORK_BUFFER + 0x1000);
    qtest_writel(q, PAIRED + 0x10, 1);
    qtest_writel(q, PAIRED + 8, 1);
    for (unsigned phase = 0; phase < 3; phase++) {
        paired_raw_input(q);
        if (phase == 1) {
            /* Leave phase0 pending until fresh phase1 RAW is retained. Its
             * real W1C retry must accept the complete duplicate future tuple.
             */
            g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 1);
            g_assert_cmphex(qtest_readl(q, PAIRED + 0x24), ==, 0x1002);
            qtest_writel(q, PAIRED + 0x24, UINT32_MAX);
        }
        pipeline_step(q, 1000000);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==,
                         phase + 1);
        paired_pixels(q, PAIRED_MAIN, phase % 2);
        paired_pixels(q, PAIRED_WORK, phase % 2);
        paired_pixels(q, PAIRED_GRID, phase);
        g_assert_cmphex(qtest_readl(q, PAIRED + 0x24), ==, 0x1002);
        if (phase != 0) {
            qtest_writel(q, PAIRED + 0x24, UINT32_MAX);
        }
        work_ack(q);
    }
    /* Phase3 names physical slot1, whose generation was consumed by phase0.
     * ACKs, kicks and time cannot reuse it; the next DMA writes slot0 first.
     */
    qtest_writel(q, PAIRED + 0x10, 1);
    pipeline_step(q, 2000000);
    g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 3);
    g_assert_cmphex(qtest_readl(q, PAIRED + 0x24), ==, 0);
    paired_raw_input(q);
    pipeline_step(q, 1000000);
    g_assert_cmpuint(property_at(q, WORK_PATH, "completed"), ==, 4);
    g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 3);
    g_assert_cmphex(qtest_readl(q, PAIRED + 0x24), ==, 0);
    work_ack(q);
    g_assert_true(g_file_set_contents(scene, (const char *)gray,
                                      sizeof(gray), NULL));
    qtest_qmp_assert_success(q,
        "{'execute':'qom-set','arguments':{'path':'/machine/cmos',"
        "'property':'image','value':%s}}", scene);
    paired_raw_input(q);
    pipeline_step(q, 1000000);
    g_assert_cmpuint(property_at(q, WORK_PATH, "completed"), ==, 5);
    g_assert_cmphex(qtest_readl(q, WORK_BUFFER + 0x1000), ==, 0x28828828);
    g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 4);
    /* Gray130 is RAW2088, not the old2056. With residual0 the quarter-history
     * blend is (2088*192 + 2056*64 + 128)/256 =2080, deviation32. Constant
     * mono and the configured matrices yield MAIN Y130/U144/V96.
     */
    paired_main_at(q, PAIRED_MAIN + 0x2000, 130, 144, 96);
    memset(expected, 0xa5, sizeof(expected));
    for (unsigned y = 0; y < 18; y++) {
        for (unsigned x = 0; x < 32; x++) {
            stw_le_p(expected + 16 + 80 * y + 2 * x, 2080);
            stw_le_p(expected + 16 + 80 * (18 + y) + 2 * x, 32);
        }
    }
    qtest_memread(q, PAIRED_WORK + 0x2000 - 16, actual, sizeof(actual));
    g_assert_cmpmem(actual, sizeof(actual), expected, sizeof(expected));
    paired_pixels(q, PAIRED_MAIN, 0);
    paired_pixels(q, PAIRED_WORK, 0);
    paired_pixels(q, PAIRED_GRID, 3);
    g_assert_cmphex(qtest_readl(q, PAIRED + 0x24), ==, 0x1402);
    qtest_writel(q, PAIRED + 0x24, UINT32_MAX);
    work_ack(q);
    /* The cyclic next phase0 names the same slot: generation5 is not replay. */
    qtest_writel(q, PAIRED + 0x10, 1);
    pipeline_step(q, 2000000);
    g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 4);
    g_assert_cmpuint(property_at(q, PAIRED_PATH, "rejected"), ==, 0);
    g_assert_cmphex(qtest_readl(q, PAIRED + 0x24), ==, 0);
    g_assert_false(qtest_get_irq(q, 99 - 32));
    qtest_quit(q);
    unlink(scene);
    unlink(bundle);
}

static void test_paired_raw_reuse_boundaries(void)
{
    static const uint32_t addresses[] = {
        WORK_BUFFER + 0x1020, /* Only identical full read spans may be reused. */
        PAIRED_MAIN,          /* A future input must not alias any output. */
        0x10020000,           /* Every phase still needs actual RAM. */
    };

    for (unsigned i = 0; i < ARRAY_SIZE(addresses); i++) {
        g_autofree char *bundle = NULL;
        g_autofree char *scene = NULL;
        QTestState *q = paired_start(&bundle, &scene, true, false);

        qtest_writel(q, PAIRED + 0x224, addresses[i]);
        qtest_writel(q, PAIRED + 0x31c, addresses[i]);
        qtest_writel(q, PAIRED + 0x10, 1);
        qtest_writel(q, PAIRED + 8, 1);
        paired_raw_input(q);
        pipeline_step(q, 1000000);
        g_assert_cmpuint(property_at(q, WORK_PATH, "completed"), ==, 1);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 0);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "rejected"), >, 0);
        paired_untouched(q);
        qtest_quit(q);
        unlink(scene);
        unlink(bundle);
    }
}

static void test_paired_publication(void)
{
    for (unsigned front_first = 0; front_first < 2; front_first++) {
        unsigned first = front_first ? 0x218 : 0x310;
        unsigned second = front_first ? 0x310 : 0x218;
        g_autofree char *bundle = NULL;
        g_autofree char *scene = NULL;
        QTestState *q = paired_start(&bundle, &scene, true, true);

        paired_raw_input(q);
        pipeline_step(q, 1000000);
        qtest_writel(q, PAIRED + 0x24, UINT32_MAX);
        work_ack(q);
        work_input(q);
        work_rise(q);
        pipeline_step(q, 1000000);
        for (unsigned phase = 0; phase < 4; phase++) {
            qtest_writel(q, PAIRED + first + 4 * phase,
                         qtest_readl(q, PAIRED + first + 4 * phase));
        }
        qtest_writel(q, PAIRED + 0x10, 1);
        pipeline_step(q, 2000000);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 1);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "cancelled"), ==, 1);
        g_assert_cmphex(qtest_readl(q, PAIRED + 0x24), ==, 0);
        for (unsigned phase = 0; phase < 4; phase++) {
            qtest_writel(q, PAIRED + second + 4 * phase,
                         qtest_readl(q, PAIRED + second + 4 * phase));
            if (phase != 3) {
                qtest_writel(q, PAIRED + 0x10, 1);
            }
            pipeline_step(q, 2000000);
            g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 1);
        }
        /* The cancelled snapshot did not consume RAW or advance phase1. */
        qtest_writel(q, PAIRED + 0x10, 1);
        pipeline_step(q, 2000000);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 2);
        paired_pixels(q, PAIRED_MAIN, 1);
        paired_pixels(q, PAIRED_GRID, 1);
        g_assert_cmphex(qtest_readl(q, PAIRED + 0x24), ==, 0x1002);
        /* A commit never clears the actual result's pending IRQ. */
        for (unsigned phase = 0; phase < 4; phase++) {
            qtest_writel(q, PAIRED + 0x218 + 4 * phase,
                         qtest_readl(q, PAIRED + 0x218 + 4 * phase));
            qtest_writel(q, PAIRED + 0x310 + 4 * phase,
                         qtest_readl(q, PAIRED + 0x310 + 4 * phase));
        }
        qtest_writel(q, PAIRED + 0x10, 1);
        g_assert_cmphex(qtest_readl(q, PAIRED + 0x24), ==, 0x1002);
        qtest_writel(q, PAIRED + 0x24, UINT32_MAX);
        work_ack(q);
        for (unsigned phase = 2; phase < 4; phase++) {
            paired_raw_input(q);
            pipeline_step(q, 1000000);
            g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==,
                             phase + 1);
            g_assert_cmphex(qtest_readl(q, PAIRED + 0x24), ==,
                            0x1002 | (phase == 3 ? 0x400 : 0));
            qtest_writel(q, PAIRED + 0x24, UINT32_MAX);
            work_ack(q);
        }
        /* Explicit stop clears an incomplete publication, never runs it. */
        qtest_writel(q, PAIRED + first,
                     qtest_readl(q, PAIRED + first));
        paired_stop(q, PAIRED);
        pipeline_step(q, 2000000);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 4);
        g_assert_cmphex(qtest_readl(q, PAIRED + 0x24), ==, 0x014a0080);
        qtest_quit(q);
        unlink(scene);
        unlink(bundle);
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
    pipeline_step(q, 1000000);
    /* Inactive descriptor staging cannot cancel the snapshotted primary job. */
    paired_secondary(q, extra);
    pipeline_step(q, 1000000);
    g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 1);
    g_assert_cmpuint(property_at(q, PAIRED_PATH, "cancelled"), ==, 0);
    paired_pixels(q, PAIRED_MAIN, 0);
    g_assert_cmphex(qtest_readl(q, PAIRED + 0x24), ==, 0x1002);
    qtest_writel(q, PAIRED + 0x24, UINT32_MAX);
    work_ack(q);
    for (unsigned phase = 1; phase < 4; phase++) {
        paired_raw_input(q);
        pipeline_step(q, 1000000);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"),
                         ==, phase + 1);
        paired_pixels(q, PAIRED_MAIN, phase % 2);
        qtest_writel(q, PAIRED + 0x24, UINT32_MAX);
        work_ack(q);
    }
    qtest_writel(q, PAIRED + 0xfc, 1);
    /* Selection is staged before a fresh complete front/back publication. */
    for (unsigned phase = 0; phase < 4; phase++) {
        qtest_writel(q, PAIRED + 0x228 + 4 * phase,
                     qtest_readl(q, PAIRED + 0x228 + 4 * phase));
        qtest_writel(q, PAIRED + 0x320 + 4 * phase,
                     qtest_readl(q, PAIRED + 0x320 + 4 * phase));
    }
    qtest_writel(q, PAIRED + 0x10, 1);
    pipeline_step(q, 2000000);
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

        paired_raw_input(q);
        pipeline_step(q, 1000000);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"),
                         ==, phase + 5);
        g_assert_cmphex(qtest_readl(q, WORK_BUFFER + extra + slot * 0x1000),
                        ==, 0x18818818);
        g_assert_cmphex(qtest_readl(q, WORK_BUFFER + slot * 0x1000),
                        ==, 0x08808808);
        paired_main_at(q, PAIRED_MAIN + extra + (phase % 2) * 0x2000,
                       130, 144, 96);
        g_assert_cmphex(qtest_readl(q, PAIRED_GRID + extra + phase * 0x1000),
                        ==, 0xa5a5a5a5);
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
    if (update) {
        /* Even an unchanged address starts an explicit publication barrier. */
        qtest_writel(q, PAIRED + 0x310, WORK_BUFFER + 0x1000);
    }
    paired_raw_input(q);
    pipeline_step(q, 1000000);
    g_assert_cmpuint(property_at(q, WORK_PATH, "completed"), ==, 5);
    g_assert_cmphex(qtest_readl(q, WORK_BUFFER + 0x1000), ==, 0x18818818);
    if (update) {
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 4);
        g_assert_cmphex(qtest_readl(q, PAIRED + 0x24), ==, 0);
        g_assert_cmphex(qtest_readl(q, PAIRED_MAIN), ==, 0xa5a5a5a5);
        for (unsigned phase = 0; phase < 4; phase++) {
            qtest_writel(q, PAIRED + 0x310 + 4 * phase,
                         WORK_BUFFER + ((phase + 1) % 4) * 0x1000);
            qtest_writel(q, PAIRED + 0x218 + 4 * phase,
                         WORK_BUFFER + ((phase + 1) % 4) * 0x1000);
            if (phase != 3) {
                /* A partial update never releases the next real RAW. */
                qtest_writel(q, PAIRED + 0x10, 1);
                pipeline_step(q, 2000000);
                g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"),
                                 ==, 4);
                g_assert_cmphex(qtest_readl(q, PAIRED + 0x24), ==, 0);
                g_assert_cmphex(qtest_readl(q, PAIRED_MAIN), ==, 0xa5a5a5a5);
            }
        }
        /* Complete address publication needs a commit; a stable ring does not. */
        pipeline_step(q, 2000000);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 4);
    }
    qtest_writel(q, PAIRED + 0x10, 1);
    qtest_writel(q, PAIRED + 0x10, 1); /* Duplicate active kick is inert. */
    pipeline_step(q, 2000000);
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
    paired_pixels(q, PAIRED_GRID, 0);
    g_assert_cmphex(qtest_readl(q, PAIRED + 0x118), ==, 5 * 56 * 28);
    g_assert_cmphex(qtest_readl(q, PAIRED + 0x24), ==, 0x1002);
    qtest_writel(q, PAIRED + 0x10, 1);
    pipeline_step(q, 2000000);
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
    pipeline_step(q, 2000000);
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

    paired_configure(q, true, peer, 7, 8, 8, 6, 51, 64, 17);
    qtest_writel(q, peer + 0x10, 1);
    qtest_writel(q, peer + 8, 1);
    work_input(q);
    work_rise(q);
    pipeline_step(q, 1000000);
    g_assert_cmpuint(property_at(q, WORK_PATH, "completed"), ==, 1);
    g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 0);
    g_assert_cmpuint(property_at(q, PAIRED_PATH, "blocked"), >, 0);
    paired_stop(q, PAIRED);
    /* The peer retries owned RAW without a new input edge. */
    pipeline_step(q, 1000000);
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
        paired_raw_input(q);
        pipeline_step(q, 1000000);
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
            pipeline_step(q, 2000000);
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

static void test_paired_online_packets(void)
{
    for (unsigned group = 3; group < 17; group++) {
        unsigned offsets[129];
        unsigned count = paired_online_offsets(group, offsets);
        unsigned holes[] = { 0, count / 2, count - 1, 63, 64, 127 };

        if (count == 1) {
            continue;
        }
        for (unsigned trial = 0; trial < (count == 129 ? 6 : 3); trial++) {
            g_autofree char *bundle = NULL, *scene = NULL;
            QTestState *q = paired_online_start(&bundle, &scene);
            uint32_t words[129];

            g_test_message("packet group=%u missing=%u", group, holes[trial]);
            for (unsigned i = 0; i < count; i++) {
                words[i] = qtest_readl(q, PAIRED + offsets[i]);
            }
            work_input(q);
            work_rise(q);
            pipeline_step(q, 1000000);
            for (unsigned i = 0; i < count; i++) {
                if (i != holes[trial]) {
                    qtest_writel(q, PAIRED + offsets[i], words[i]);
                }
            }
            pipeline_step(q, 2000000);
            g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 1);
            g_assert_cmpuint(property_at(q, PAIRED_PATH, "cancelled"), ==, 0);
            paired_pixels(q, PAIRED_MAIN, 0);
            paired_pixels(q, PAIRED_WORK, 0);
            paired_pixels(q, PAIRED_GRID, 0);
            qtest_writel(q, PAIRED + 0x24, UINT32_MAX);
            work_ack(q);
            paired_raw_input(q);
            pipeline_step(q, 1000000);
            g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 1);
            g_assert_cmpuint(property_at(q, PAIRED_PATH, "rejected"), ==, 0);
            paired_main_at(q, PAIRED_MAIN + 0x2000, 0xa5, 0xa5, 0xa5);
            /* Even previously covered words cannot finish this fresh round. */
            qtest_writel(q, PAIRED + offsets[0], words[0]);
            pipeline_step(q, 1000000);
            g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 1);
            for (unsigned i = 1; i < count; i++) {
                qtest_writel(q, PAIRED + offsets[i], words[i]);
            }
            pipeline_step(q, 999999);
            g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 1);
            pipeline_step(q, 1);
            g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 2);
            g_assert_cmpuint(property_at(q, PAIRED_PATH, "rejected"), ==, 0);
            paired_pixels(q, PAIRED_MAIN, 1);
            paired_pixels(q, PAIRED_WORK, 1);
            paired_pixels(q, PAIRED_GRID, 1);
            qtest_quit(q);
            unlink(scene);
            unlink(bundle);
        }
    }
}

static void test_paired_online_invalid(void)
{
    for (unsigned group = 0; group < 17; group++) {
        g_autofree char *bundle = NULL, *scene = NULL;
        QTestState *q = paired_online_start(&bundle, &scene);
        unsigned offsets[129], count = paired_online_offsets(group, offsets);
        unsigned bad = group == 1 ? 2 : group == 10 ? 1 :
                       count == 129 ? 128 : 0;
        uint32_t words[129];

        g_test_message("invalid future packet group=%u", group);
        for (unsigned i = 0; i < count; i++) {
            words[i] = qtest_readl(q, PAIRED + offsets[i]);
        }
        work_input(q);
        work_rise(q);
        pipeline_step(q, 1000000);
        for (unsigned i = 0; i < count; i++) {
            qtest_writel(q, PAIRED + offsets[i], i == bad ? UINT32_MAX : words[i]);
        }
        pipeline_step(q, 2000000);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 1);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "cancelled"), ==, 0);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "rejected"), ==, 0);
        paired_pixels(q, PAIRED_MAIN, 0);
        paired_pixels(q, PAIRED_WORK, 0);
        paired_pixels(q, PAIRED_GRID, 0);
        qtest_writel(q, PAIRED + 0x24, UINT32_MAX);
        work_ack(q);
        paired_raw_input(q);
        pipeline_step(q, 1000000);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 1);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "rejected"), >, 0);
        paired_main_at(q, PAIRED_MAIN + 0x2000, 0xa5, 0xa5, 0xa5);
        g_assert_cmphex(qtest_readl(q, PAIRED_WORK + 0x2000), ==, 0xa5a5a5a5);
        for (unsigned i = 0; i < count; i++) {
            qtest_writel(q, PAIRED + offsets[i], words[i]);
        }
        pipeline_step(q, 2000000);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 2);
        paired_pixels(q, PAIRED_MAIN, 1);
        paired_pixels(q, PAIRED_WORK, 1);
        paired_pixels(q, PAIRED_GRID, 1);
        qtest_quit(q);
        unlink(scene);
        unlink(bundle);
    }
}

static void test_paired_online_results(void)
{
    static const uint8_t next[][3] = {
        { 0, 128, 128 }, { 0, 144, 160 }, { 0, 144, 96 },
        { 129, 96, 144 }, { 64, 136, 112 },
    };

    for (unsigned test = 0; test < ARRAY_SIZE(next); test++) {
        g_autofree char *bundle = NULL, *scene = NULL;
        QTestState *q = paired_start(&bundle, &scene, true, false);

        if (!test) {
            qtest_writel(q, PAIRED + 0x700, 0x100);
            qtest_writel(q, PAIRED + 0x708, 0x02000200);
            qtest_writel(q, PAIRED + 0x70c, 0x02000200);
            qtest_writel(q, PAIRED + 0x720, 32767);
        }
        qtest_writel(q, PAIRED + 0x10, 1);
        qtest_writel(q, PAIRED + 8, 1);
        work_input(q);
        work_rise(q);
        pipeline_step(q, 1000000);
        if (!test) {
            unsigned offsets[129], count = paired_online_offsets(5, offsets);
            uint32_t words[7];

            for (unsigned i = 0; i < count; i++) {
                words[i] = i < 2 ? 0 : qtest_readl(q, PAIRED + offsets[i]);
            }
            for (unsigned i = 0; i < count; i++) {
                qtest_writel(q, PAIRED + offsets[i], words[i]);
            }
        } else if (test == 1) {
            paired_online_republish(q, 0xb00, 0);
        } else if (test == 2) {
            paired_online_republish(q, 0xb64, 0);
        } else if (test == 3) {
            static const int swap[] = { 1024, 0, 0, 0, 0, 1024, 0, 1024, 0 };

            paired_matrix(q, PAIRED, 0xb94, swap, 13, true);
            paired_online_republish(q, 0xbac, 0x01000000);
        } else {
            paired_curve(q, 0, 512, 129, false);
        }
        pipeline_step(q, 2000000);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 1);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "cancelled"), ==, 0);
        paired_pixels(q, PAIRED_MAIN, 0);
        paired_pixels(q, PAIRED_WORK, 0);
        paired_pixels(q, PAIRED_GRID, 0);
        qtest_writel(q, PAIRED + 0x24, UINT32_MAX);
        work_ack(q);
        paired_raw_input(q);
        pipeline_step(q, 1000000);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 2);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "rejected"), ==, 0);
        paired_main_at(q, PAIRED_MAIN + 0x2000,
                       next[test][0], next[test][1], next[test][2]);
        paired_pixels(q, PAIRED_WORK, 1);
        paired_pixels(q, PAIRED_GRID, 1);
        qtest_quit(q);
        unlink(scene);
        unlink(bundle);
    }
}

static void test_paired_online_history_race(void)
{
    static const uint8_t gray[] = "P5\n1 1\n255\n\201";
    g_autofree char *bundle = NULL, *scene = NULL;
    QTestState *q = paired_start(&bundle, &scene, true, true);

    for (unsigned phase = 0; phase < 2; phase++) {
        paired_raw_input(q);
        pipeline_step(q, 1000000);
        qtest_writel(q, PAIRED + 0x24, UINT32_MAX);
        work_ack(q);
    }
    g_assert_true(g_file_set_contents(scene, (const char *)gray,
                                      sizeof(gray) - 1, NULL));
    qtest_qmp_assert_success(q,
        "{'execute':'qom-set','arguments':{'path':'/machine/cmos',"
        "'property':'image','value':%s}}", scene);
    work_input(q);
    work_rise(q);
    pipeline_step(q, 1000000);
    /* The old job has private history. Changing a complete future precolor
     * packet invalidates future history without cancelling that calculation.
     */
    qtest_writel(q, PAIRED + 0x75c, qtest_readl(q, PAIRED + 0x75c));
    qtest_writel(q, PAIRED + 0x76c, 0x04010000);
    /* Abandon that round. Its dirty word is now equal on readback, but must
     * still invalidate old history when this fresh whole packet completes.
     */
    paired_online_republish(q, 0x76c, 0x04010000);
    qtest_writew(q, PAIRED_WORK, 0);
    pipeline_step(q, 2000000);
    g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 3);
    g_assert_cmpuint(property_at(q, PAIRED_PATH, "cancelled"), ==, 0);
    g_assert_cmpuint(qtest_readw(q, PAIRED_WORK), ==, 2068);
    g_assert_cmpuint(qtest_readw(q, PAIRED_WORK + 18 * 80), ==, 16);
    for (unsigned phase = 3; phase < 5; phase++) {
        qtest_writel(q, PAIRED + 0x24, UINT32_MAX);
        work_ack(q);
        paired_raw_input(q);
        pipeline_step(q, 1000000);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, phase + 1);
        unsigned address = PAIRED_WORK + (phase % 2) * 0x2000;

        /* In particular, phase0 must not reuse phase2's old-revision result. */
        g_assert_cmpuint(qtest_readw(q, address), ==, 2072);
        g_assert_cmpuint(qtest_readw(q, address + 18 * 80), ==, 0);
    }
    g_assert_cmpuint(property_at(q, PAIRED_PATH, "rejected"), ==, 0);
    qtest_quit(q);
    unlink(scene);
    unlink(bundle);
}

static void paired_capacity_wait(QTestState *q)
{
    QDict *reply = qtest_qmp(q, "{'execute':'qom-get','arguments':"
        "{'path':'/machine/paired-processing','property':'runtime'}}");
    const char *state = qdict_get_str(reply, "return");

    g_assert_nonnull(strstr(state, " inflight=1 "));
    g_assert_nonnull(strstr(state, " compute=2 "));
    g_assert_nonnull(strstr(state, " stage=7 remaining=0 capacity-wait=1"));
    qobject_unref(reply);
}

static void paired_capacity_fill(QTestState *q, const char *scene, const char *gate)
{
    static const uint8_t gray[] = {
        'P', '5', '\n', '1', ' ', '1', '\n', '2', '5', '5', '\n', 64,
    };

    paired_gate_main(q);
    paired_stream(q, PAIRED);
    qtest_writel(q, PAIRED + 0x10, 1);
    qtest_writel(q, PAIRED + 8, 1);
    paired_raw_input(q);
    pipeline_step(q, 1000000);
    g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 1);
    paired_stream_pixels(q, gate, true);
    qtest_writel(q, PAIRED + 0x24, UINT32_MAX);
    work_ack(q);
    g_assert_true(g_file_set_contents(scene, (const char *)gray, sizeof(gray), NULL));
    qtest_qmp_assert_success(q,
        "{'execute':'qom-set','arguments':{'path':'/machine/cmos',"
        "'property':'image','value':%s}}", scene);
    paired_raw_input(q);
    pipeline_step(q, 1000000);
    paired_capacity_wait(q);
    g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 1);
    g_assert_cmpuint(property_at(q, PAIRED_PATH, "rejected"), ==, 0);
    g_assert_cmpuint(property_at(q, gate, "input-frames"), ==, 1);
    g_assert_cmpuint(property_at(q, gate, "processed"), ==, 0);
    g_assert_cmphex(qtest_readl(q, PAIRED + 0x24), ==, 0);
    paired_stream_pixels(q, gate, true);
    paired_untouched(q);
    for (unsigned phase = 0; phase < 4; phase++) {
        paired_gate_output(q, phase, false, 0, 0, 0);
    }
    g_assert_false(qtest_get_irq(q, 102 - 32));
}

static void test_paired_capacity(void)
{
    g_autofree char *bundle = NULL;
    g_autofree char *scene = NULL;
    QTestState *q = paired_start(&bundle, &scene, true, false);
    g_autofree char *gate = paired_gate_path(q);

    paired_capacity_fill(q, scene, gate);
    qtest_writel(q, PAIRED_GATE + 4, 1);
    pipeline_step(q, 999999);
    paired_capacity_wait(q);
    paired_gate_output(q, 0, false, 0, 0, 0);
    pipeline_step(q, 1);
    /* Old input really reached all three DMA outputs before capacity returned.
     * That release wakes exactly the already computed new result, with no new
     * RAW edge, ACK, forced slot release or extra virtual-time latency.
     */
    g_assert_cmpuint(property_at(q, gate, "processed"), ==, 1);
    g_assert_cmpuint(property_at(q, gate, "processing-inflight"), ==, 0);
    g_assert_cmpuint(property_at(q, gate, "input-frames"), ==, 2);
    g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 2);
    g_assert_cmpuint(property_at(q, PAIRED_PATH, "rejected"), ==, 0);
    paired_gate_output(q, 0, true, 129, 144, 96);
    paired_stream_samples(q, gate, true, 64, 136, 112);
    g_assert_cmphex(qtest_readl(q, PAIRED_GATE + 0x1c), ==, 0x61);
    g_assert_cmphex(qtest_readl(q, PAIRED_GATE + 0x20), ==, 8);
    g_assert_true(qtest_get_irq(q, 102 - 32));
    g_assert_cmphex(qtest_readl(q, PAIRED + 0x24), ==, 0x1002);
    paired_outputs_untouched(q);
    g_assert_false(qtest_get_irq(q, 99 - 32));
    /* Both ordinary pending words fence reuse of the sole public input. */
    pipeline_step(q, 2000000);
    paired_gate_output(q, 1, false, 0, 0, 0);
    qtest_writel(q, PAIRED_GATE + 0x1c, 0x61);
    pipeline_step(q, 2000000);
    g_assert_cmpuint(property_at(q, gate, "processed"), ==, 1);
    paired_gate_output(q, 1, false, 0, 0, 0);
    paired_stream_samples(q, gate, true, 64, 136, 112);
    /* A third actual RAW remains retained behind paired's own pending word. */
    work_ack(q);
    paired_raw_input(q);
    pipeline_step(q, 1000000);
    g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 2);
    qtest_writel(q, PAIRED_GATE + 0x20, 8);
    pipeline_step(q, 999999);
    paired_gate_output(q, 1, false, 0, 0, 0);
    pipeline_step(q, 1);
    g_assert_cmpuint(property_at(q, gate, "processed"), ==, 2);
    paired_gate_output(q, 1, true, 64, 136, 112);
    paired_stream_samples(q, gate, false, 0, 0, 0);
    /* Capacity cannot bypass paired's own unacknowledged result. */
    g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 2);
    g_assert_cmpuint(property_at(q, gate, "input-frames"), ==, 2);
    qtest_writel(q, PAIRED + 0x24, UINT32_MAX);
    pipeline_step(q, 999999);
    g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 2);
    pipeline_step(q, 1);
    g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 3);
    g_assert_cmpuint(property_at(q, gate, "input-frames"), ==, 3);
    paired_stream_samples(q, gate, true, 64, 136, 112);
    paired_gate_output(q, 2, false, 0, 0, 0);
    qtest_quit(q);
    unlink(bundle);
    unlink(scene);
}

static void test_paired_capacity_cancel(void)
{
    for (unsigned mode = 0; mode < 5; mode++) {
        g_autofree char *bundle = NULL;
        g_autofree char *scene = NULL;
        QTestState *q = paired_start(&bundle, &scene, true, false);
        g_autofree char *gate = paired_gate_path(q);

        paired_capacity_fill(q, scene, gate);
        if (mode == 0) {
            /* Commit a different route, then restore the accepted tuple.
             * Main-loop gaps may already reject the old result; equality
             * cannot restore its epoch in either ordering.
             */
            paired_gate_input(q, 9);
            paired_gate_input(q, 8);
        } else if (mode == 1) {
            qtest_writel(q, PAIRED_GATE + 4, 0);
            qtest_writel(q, PAIRED_GATE + 4, 1);
        } else if (mode == 2) {
            paired_gate_slot(q, 54);
            paired_gate_slot(q, 56);
        } else if (mode == 3) {
            qtest_system_reset(q);
        } else {
            paired_stop(q, PAIRED);
        }
        /* Check the revoked ready result before another numerical deadline.
         * A later1ms deadline can legitimately recompute unconsumed RAW under
         * the restored epoch; it is distinct from publishing that old result.
         * The paired stop has its separate normal drain deadline.
         */
        pipeline_step(q, mode == 4 ? 2000000 : 1);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "completed"), ==, 1);
        g_assert_cmpuint(property_at(q, PAIRED_PATH, "rejected"), ==, mode < 3);
        g_assert_cmpuint(property_at(q, gate, "processed"), ==, 0);
        g_assert_cmpuint(property_at(q, gate, "input-frames"), ==, mode == 3 ? 0 : 1);
        paired_stream_pixels(q, gate, mode == 4);
        paired_outputs_untouched(q);
        g_assert_false(qtest_get_irq(q, 99 - 32));
        for (unsigned phase = 0; phase < 4; phase++) {
            paired_gate_output(q, phase, false, 0, 0, 0);
        }
        if (mode == 4) {
            g_assert_cmphex(qtest_readl(q, PAIRED + 0x24), ==, 0x014a0080);
        } else {
            g_assert_cmphex(qtest_readl(q, PAIRED + 0x24), ==, 0);
        }
        g_assert_false(qtest_get_irq(q, 102 - 32));
        qtest_quit(q);
        unlink(bundle);
        unlink(scene);
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
    qtest_add_func("/nikon/cmos/pipeline/image-cfa", test_image_cfa_transport);
    qtest_add_func("/nikon/cmos/pipeline/cfa-response", test_image_cfa_response);
    qtest_add_func("/nikon/cmos/period", test_period);
    qtest_add_func("/nikon/cmos/pipeline/valid-silent",
                   test_pipeline_valid_and_silent);
    qtest_add_func("/nikon/cmos/pipeline/cpu-lifecycle",
                   test_pipeline_cpu_lifecycle);
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
    qtest_add_func("/nikon/cmos/statistics/rgb", test_statistics_rgb);
    qtest_add_func("/nikon/cmos/statistics/stage-completion",
                   test_statistics_stage_completion);
    qtest_add_func("/nikon/cmos/statistics/ring-completion",
                   test_statistics_ring_completion);
    qtest_add_func("/nikon/cmos/statistics/storage-period",
                   test_statistics_storage_period);
    qtest_add_func("/nikon/cmos/pipeline/main-input", test_main_input);
    qtest_add_func("/nikon/cmos/capacity/front", test_front_capacity);
    qtest_add_func("/nikon/cmos/capacity/domains", test_capture_domains);
    qtest_add_func("/nikon/cmos/capacity/statistics", test_statistics_capacity);
    qtest_add_func("/nikon/cmos/capacity/group", test_group_capacity);
    qtest_add_func("/nikon/cmos/pipeline/two-views", test_acquisition_views);
    qtest_add_func("/nikon/cmos/pipeline/view-cancellation",
                   test_acquisition_cancellation);
    qtest_add_func("/nikon/cmos/pipeline/view-late-request",
                   test_acquisition_late_request);
    qtest_add_func("/nikon/cmos/pipeline/view-boundary",
                   test_acquisition_boundary);
    qtest_add_func("/nikon/cmos/group/payload", test_group_payload);
    qtest_add_func("/nikon/cmos/group/input-ready", test_group_ready);
    qtest_add_func("/nikon/cmos/group/commit-input", test_group_commit_input);
    qtest_add_func("/nikon/cmos/group/boundaries", test_group_boundaries);
    qtest_add_func("/nikon/cmos/group/cancellation", test_group_cancellation);
    qtest_add_func("/nikon/cmos/group/families", test_group_families);
    qtest_add_func("/nikon/cmos/group/worker", test_group_worker);
    qtest_add_func("/nikon/cmos/group/worker-cancellation", test_group_worker_cancel);
    qtest_add_func("/nikon/cmos/pipeline/main-lifecycle",
                   test_main_input_lifecycle);
    qtest_add_func("/nikon/cmos/pipeline/main-boundaries",
                   test_main_input_boundaries);
    qtest_add_func("/nikon/cmos/pipeline/main-masked-stream",
                   test_main_input_masked_stream);
    qtest_add_func("/nikon/cmos/statistics/saturation",
                   test_statistics_saturation);
    qtest_add_func("/nikon/cmos/statistics/job-ownership",
                   test_statistics_job_ownership);
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
    qtest_add_func("/nikon/cmos/processing/snapshot", test_capture_work_snapshot);
    qtest_add_func("/nikon/cmos/processing/submitted-cancel",
                   test_capture_work_submitted_cancel);
    qtest_add_func("/nikon/cmos/processing/main-domain",
                   test_capture_work_main_domain);
    qtest_add_func("/nikon/cmos/paired/transaction", test_paired_processing);
    qtest_add_func("/nikon/cmos/paired/preflight", test_paired_preflight);
    qtest_add_func("/nikon/cmos/paired/lifecycle", test_paired_lifecycle);
    qtest_add_func("/nikon/cmos/paired/coverage-raw",
                   test_paired_coverage_and_raw_failure);
    qtest_add_func("/nikon/cmos/paired/preserve-results",
                   test_paired_preserve_results);
    qtest_add_func("/nikon/cmos/paired/backpressure", test_paired_backpressure);
    qtest_add_func("/nikon/cmos/paired/c-matrix", test_paired_c_matrix);
    qtest_add_func("/nikon/cmos/paired/post/transaction", test_paired_post);
    qtest_add_func("/nikon/cmos/paired/online/gain", test_paired_online_gain);
    qtest_add_func("/nikon/cmos/paired/online/boundaries",
                   test_paired_online_boundaries);
    qtest_add_func("/nikon/cmos/paired/online/packets", test_paired_online_packets);
    qtest_add_func("/nikon/cmos/paired/online/invalid", test_paired_online_invalid);
    qtest_add_func("/nikon/cmos/paired/online/results", test_paired_online_results);
    qtest_add_func("/nikon/cmos/paired/online/history-race",
                   test_paired_online_history_race);
    qtest_add_func("/nikon/cmos/paired/main-gain/transaction",
                   test_paired_main_gain);
    qtest_add_func("/nikon/cmos/paired/main-gain/spatial",
                   test_paired_main_gain_spatial);
    qtest_add_func("/nikon/cmos/paired/main-gain/boundaries",
                   test_paired_main_gain_boundaries);
    qtest_add_func("/nikon/cmos/paired/main-gain/lifecycle",
                   test_paired_main_gain_lifecycle);
    qtest_add_func("/nikon/cmos/paired/post/boundaries",
                   test_paired_post_boundaries);
    qtest_add_func("/nikon/cmos/paired/post/packet", test_paired_post_packet);
    qtest_add_func("/nikon/cmos/paired/post/lifecycle",
                   test_paired_post_lifecycle);
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
    qtest_add_func("/nikon/cmos/paired/cycle", test_paired_cycle);
    qtest_add_func("/nikon/cmos/paired/raw-reuse", test_paired_raw_reuse);
    qtest_add_func("/nikon/cmos/paired/raw-reuse-boundaries",
                   test_paired_raw_reuse_boundaries);
    qtest_add_func("/nikon/cmos/paired/cfa/reduction", test_paired_cfa_reduction);
    qtest_add_func("/nikon/cmos/paired/cfa/color", test_paired_cfa_color);
    qtest_add_func("/nikon/cmos/paired/transfer/transaction", test_paired_transfer);
    qtest_add_func("/nikon/cmos/paired/transfer/boundaries",
                   test_paired_transfer_boundaries);
    qtest_add_func("/nikon/cmos/paired/publication", test_paired_publication);
    qtest_add_func("/nikon/cmos/paired/stop-drain-race",
                   test_paired_stop_drain_race);
    qtest_add_func("/nikon/cmos/paired/peer-release", test_paired_peer_release);
    qtest_add_func("/nikon/cmos/processing/geometry-reduction",
                   test_capture_geometry_reduction);
    qtest_add_func("/nikon/cmos/paired/calibration-rejected",
                   test_paired_calibration_rejected);
    qtest_add_func("/nikon/cmos/paired/noise", test_paired_noise);
    qtest_add_func("/nikon/cmos/paired/online/noise", test_paired_noise_update);
    qtest_add_func("/nikon/cmos/paired/selections", test_paired_selections);
    qtest_add_func("/nikon/cmos/paired/filter/transaction", test_paired_filter);
    qtest_add_func("/nikon/cmos/paired/filter/boundaries",
                   test_paired_filter_boundaries);
    qtest_add_func("/nikon/cmos/paired/filter/lifecycle",
                   test_paired_filter_lifecycle);
    qtest_add_func("/nikon/cmos/paired/denoise/transaction", test_paired_denoise);
    qtest_add_func("/nikon/cmos/paired/denoise/boundaries",
                   test_paired_denoise_boundaries);
    qtest_add_func("/nikon/cmos/paired/denoise/lifecycle",
                   test_paired_denoise_lifecycle);
    qtest_add_func("/nikon/cmos/paired/detail/transaction", test_paired_detail);
    qtest_add_func("/nikon/cmos/paired/colorfilter/transaction",
                   test_paired_colorfilter);
    qtest_add_func("/nikon/cmos/paired/saturation/transaction",
                   test_paired_saturation);
    qtest_add_func("/nikon/cmos/paired/saturation/boundaries",
                   test_paired_saturation_boundaries);
    qtest_add_func("/nikon/cmos/paired/saturation/lifecycle",
                   test_paired_saturation_lifecycle);
    qtest_add_func("/nikon/cmos/paired/saturation/history",
                   test_paired_saturation_history);
    qtest_add_func("/nikon/cmos/paired/saturation/staging",
                   test_paired_saturation_staging);
    qtest_add_func("/nikon/cmos/paired/colorfilter/boundaries",
                   test_paired_colorfilter_boundaries);
    qtest_add_func("/nikon/cmos/paired/colorfilter/publication",
                   test_paired_colorfilter_publication);
    qtest_add_func("/nikon/cmos/paired/colorfilter/lifecycle",
                   test_paired_colorfilter_lifecycle);
    qtest_add_func("/nikon/cmos/paired/colorfilter/history",
                   test_paired_colorfilter_history);
    qtest_add_func("/nikon/cmos/paired/radial/transaction", test_paired_radial);
    qtest_add_func("/nikon/cmos/paired/radial/boundaries",
                   test_paired_radial_boundaries);
    qtest_add_func("/nikon/cmos/paired/radial/lifecycle",
                   test_paired_radial_lifecycle);
    qtest_add_func("/nikon/cmos/paired/radial/tables",
                   test_paired_radial_tables);
    qtest_add_func("/nikon/cmos/paired/radial/tables-lifecycle",
                   test_paired_radial_tables_lifecycle);
    qtest_add_func("/nikon/cmos/paired/detail/boundaries",
                   test_paired_detail_boundaries);
    qtest_add_func("/nikon/cmos/paired/detail/lifecycle",
                   test_paired_detail_lifecycle);
    qtest_add_func("/nikon/cmos/paired/adaptive/transaction",
                   test_paired_adaptive);
    qtest_add_func("/nikon/cmos/paired/adaptive/boundaries",
                   test_paired_adaptive_boundaries);
    qtest_add_func("/nikon/cmos/paired/adaptive/lifecycle",
                   test_paired_adaptive_lifecycle);
    qtest_add_func("/nikon/cmos/paired/adaptive/history",
                   test_paired_adaptive_history);
    qtest_add_func("/nikon/cmos/paired/photo", test_paired_photo);
    qtest_add_func("/nikon/cmos/paired/stride", test_paired_stride);
    qtest_add_func("/nikon/cmos/paired/stream/capacity", test_paired_capacity);
    qtest_add_func("/nikon/cmos/paired/stream/capacity-cancel",
                   test_paired_capacity_cancel);
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

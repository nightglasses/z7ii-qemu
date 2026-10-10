/*
 * GUI first-frame acceptance observations.
 *
 * This test deliberately performs no frame handoff, callback injection, or
 * pixel generation.  It samples the diagnostic descriptor aperture and the
 * machine object tree, then reports which pieces of the real runtime evidence
 * are still missing.  A cold machine therefore remains an explicit UNMET
 * result rather than a synthetic GUI pass.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "qobject/qdict.h"
#include "qobject/qlist.h"
#include "libqtest.h"
#include "hw/nikon/frame.h"

#define FRAME_BASE 0x24081000

enum GuiEvidence {
    GUI_CALLBACK = 1u << 0,
    GUI_BUFFER = 1u << 1,
    GUI_GEOMETRY = 1u << 2,
    GUI_STRIDE = 1u << 3,
    GUI_FORMAT = 1u << 4,
    GUI_COMPLETION = 1u << 5,
    GUI_SAVE = 1u << 6,
};

static char *image;

static QTestState *start(void)
{
    g_autofree char *bios = g_shell_quote(image);

    return qtest_initf("-M z7ii-mas-diagnostic,frame-contract=on -smp 1 "
                       "-bios %s -nodefaults -display none", bios);
}

static bool machine_has_child(QTestState *q, const char *type)
{
    QDict *response = qtest_qmp(q, "{'execute':'qom-list',"
                                "'arguments':{'path':'/machine'}}");
    const QListEntry *entry;
    g_autofree char *child_type = g_strdup_printf("child<%s>", type);
    bool found = false;

    g_assert_true(qdict_haskey(response, "return"));
    QLIST_FOREACH_ENTRY(qdict_get_qlist(response, "return"), entry) {
        const QDict *child = qobject_to(QDict, qlist_entry_obj(entry));

        if (!strcmp(qdict_get_str(child, "type"), child_type)) {
            found = true;
            break;
        }
    }
    qobject_unref(response);
    return found;
}

static uint64_t readout_property(QTestState *q, const char *property)
{
    QDict *reply;

    if (!strcmp(property, "requests")) {
        reply = qtest_qmp(q, "{'execute':'qom-get','arguments':"
                           "{'path':'/machine/readout-control',"
                           "'property':'requests'}}");
    } else if (!strcmp(property, "completion-generation")) {
        reply = qtest_qmp(q, "{'execute':'qom-get','arguments':"
                           "{'path':'/machine/readout-control',"
                           "'property':'completion-generation'}}");
    } else {
        g_assert_not_reached();
    }
    g_assert_true(qdict_haskey(reply, "return"));
    uint64_t value = qdict_get_uint(reply, "return");

    qobject_unref(reply);
    return value;
}

static void report_unmet(uint32_t unmet)
{
    static const struct {
        uint32_t bit;
        const char *name;
    } evidence[] = {
        { GUI_CALLBACK, "callback" },
        { GUI_BUFFER, "buffer" },
        { GUI_GEOMETRY, "geometry" },
        { GUI_STRIDE, "stride" },
        { GUI_FORMAT, "format" },
        { GUI_COMPLETION, "completion" },
        { GUI_SAVE, "save" },
    };
    GString *missing = g_string_new(NULL);
    unsigned i;

    for (i = 0; i < ARRAY_SIZE(evidence); i++) {
        if (unmet & evidence[i].bit) {
            if (missing->len) {
                g_string_append(missing, ",");
            }
            g_string_append(missing, evidence[i].name);
        }
    }
    g_test_message("GUI acceptance UNMET: %s", missing->str);
    g_string_free(missing, true);
}

static void test_observations(void)
{
    uint32_t stage;
    uint32_t width, height, stride, bytes_per_pixel, format;
    uint64_t source, destination;
    uint64_t readout_requests, completion_generation;
    uint32_t unmet = GUI_CALLBACK | GUI_BUFFER | GUI_GEOMETRY |
                     GUI_STRIDE | GUI_FORMAT | GUI_COMPLETION | GUI_SAVE;
    QTestState *q;
    uint32_t idle = cpu_to_le32(0xeafffffe);

    g_assert_true(g_file_set_contents(image, (char *)&idle, sizeof(idle), NULL));
    q = start();

    /* Observation only: the cold descriptor must be unbound and empty. */
    stage = qtest_readl(q, FRAME_BASE + 0xc0);
    source = qtest_readl(q, FRAME_BASE + 0x00) |
             ((uint64_t)qtest_readl(q, FRAME_BASE + 0x04) << 32);
    destination = qtest_readl(q, FRAME_BASE + 0x08) |
                  ((uint64_t)qtest_readl(q, FRAME_BASE + 0x0c) << 32);
    width = qtest_readl(q, FRAME_BASE + 0x10);
    height = qtest_readl(q, FRAME_BASE + 0x14);
    stride = qtest_readl(q, FRAME_BASE + 0x18);
    bytes_per_pixel = qtest_readl(q, FRAME_BASE + 0x1c);
    format = qtest_readl(q, FRAME_BASE + 0x20);
    readout_requests = readout_property(q, "requests");
    completion_generation = readout_property(q, "completion-generation");

    g_assert_cmpuint(stage, ==, NIKON_FRAME_UNBOUND);
    g_assert_cmpuint(source, ==, 0);
    g_assert_cmpuint(destination, ==, 0);
    g_assert_cmpuint(width, ==, 0);
    g_assert_cmpuint(height, ==, 0);
    g_assert_cmpuint(stride, ==, 0);
    g_assert_cmpuint(bytes_per_pixel, ==, 0);
    g_assert_cmpuint(format, ==, 0);

    /* Each bit is cleared only by evidence observed from a real producer. */
    if (source || destination) {
        unmet &= ~GUI_BUFFER;
    }
    if (width && height) {
        unmet &= ~GUI_GEOMETRY;
    }
    if (stride && bytes_per_pixel) {
        unmet &= ~GUI_STRIDE;
    }
    if (format) {
        unmet &= ~GUI_FORMAT;
    }
    if (readout_requests) {
        unmet &= ~GUI_CALLBACK;
    }
    if (completion_generation) {
        unmet &= ~GUI_COMPLETION;
    }
    if (machine_has_child(q, "nikon-save")) {
        /* The save child is only an observable endpoint; no request is made. */
        unmet &= ~GUI_SAVE;
    }

    report_unmet(unmet);
    g_assert_cmpuint(unmet, !=, 0);
    qtest_quit(q);
}

int main(int argc, char **argv)
{
    int fd, result;

    g_test_init(&argc, &argv, NULL);
    fd = g_file_open_tmp("nikonguiXXXXXX", &image, NULL);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    qtest_add_func("/nikon/gui/observations", test_observations);
    result = g_test_run();
    unlink(image);
    g_free(image);
    return result;
}
